#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <gst/gst.h>

#include "gst_decoder.h"
#include "gst_display.h"
#include "yolov5_infer.h"
#include "image_drawing.h"
#include "common.h"

#define MAX_CHANNEL        4
#define CONFIG_LINE_MAX    256
#define CANVAS_W           1280
#define CANVAS_H           960

static volatile bool g_running = true;

typedef struct {
    int                  id;
    GstDecoder          *decoder;
    rknn_app_context_t   yolo_ctx;
    char                 rtsp[256];
    image_buffer_t       frame_buf;      
    GMutex               frame_lock;
} ChannelContext;

typedef struct {
    char                 model_path[256];
    ChannelContext       channels[MAX_CHANNEL];
    int                  channel_count;
} AppConfig;

AppConfig g_cfg = {0};

void sigint_handler(int sig) {
    g_running = false;
    printf("\n退出...\n");
}

int load_config(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char line[CONFIG_LINE_MAX];
    g_cfg.channel_count = 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (strstr(line, "path =")) sscanf(line, "path = %s", g_cfg.model_path);
        if (strstr(line, "rtsp =")) {
            ChannelContext *ch = &g_cfg.channels[g_cfg.channel_count];
            ch->id = g_cfg.channel_count + 1;
            sscanf(line, "rtsp = %s", ch->rtsp);
            g_cfg.channel_count++;
        }
    }
    fclose(f);
    return 0;
}


static void image_callback(int width, int height, const char *format,
                           void *data, size_t data_size, void *user_data) {
    ChannelContext *ctx = (ChannelContext *)user_data;
    if (!ctx || !g_running) return;

    g_mutex_lock(&ctx->frame_lock);

    // 直接覆盖，不新建
    memcpy(ctx->frame_buf.virt_addr, data, data_size);
    ctx->frame_buf.width = width;
    ctx->frame_buf.height = height;
    ctx->frame_buf.width_stride = width;
    ctx->frame_buf.height_stride = height;
    ctx->frame_buf.format = IMAGE_FORMAT_RGB888;

    // 推理
    object_detect_result_list od_results = {0};
    yolov5_infer(&ctx->yolo_ctx, &ctx->frame_buf, &od_results);

    // 画框
    for (int i = 0; i < od_results.count; i++) {
        auto res = &od_results.results[i];
        int x = res->box.left;
        int y = res->box.top;
        int w = res->box.right - x;
        int h = res->box.bottom - y;
        draw_rectangle(&ctx->frame_buf, x, y, w, h, 0xFFFF0000, 2);
    }

    g_mutex_unlock(&ctx->frame_lock);
}


void composite_2x2_rga(uint8_t *canvas, ChannelContext *channels, int count)
{
    memset(canvas, 0, CANVAS_W * CANVAS_H * 3);

    const int cell_cols = 2;
    const int cell_rows = 2;
    const int cell_w = CANVAS_W / cell_cols;
    const int cell_h = CANVAS_H / cell_rows;

    for (int i = 0; i < count && i < MAX_CHANNEL; i++) {
        ChannelContext *ch = &channels[i];
        g_mutex_lock(&ch->frame_lock);

        image_buffer_t *img = &ch->frame_buf;
        if (!img->virt_addr || img->width <= 0 || img->height <= 0) {
            g_mutex_unlock(&ch->frame_lock);
            continue;
        }

        // 自适应居中
        float scale = MIN((float)cell_w / img->width, (float)cell_h / img->height);
        int draw_w = img->width * scale;
        int draw_h = img->height * scale;
        int ox = (cell_w - draw_w) / 2;
        int oy = (cell_h - draw_h) / 2;

        int dst_x = (i % 2) * cell_w + ox;
        int dst_y = (i / 2) * cell_h + oy;

        // 软合成（低占用、不触发 RGA 崩溃）
        for (int y = 0; y < draw_h; y++) {
            int sy = y / scale;
            uint8_t *s = img->virt_addr + sy * img->width * 3;
            uint8_t *d = canvas + (dst_y + y) * CANVAS_W * 3 + dst_x * 3;
            for (int x = 0; x < draw_w; x++) {
                int sx = x / scale;
                d[x*3+0] = s[sx*3+0];
                d[x*3+1] = s[sx*3+1];
                d[x*3+2] = s[sx*3+2];
            }
        }

        g_mutex_unlock(&ch->frame_lock);
    }
}

// ==============================
// 主函数
// ==============================
int main(int argc, char *argv[]) {
    signal(SIGINT, sigint_handler);
    gst_init(NULL, NULL);

    if (load_config("./config/config.ini") < 0) return -1;

    // 初始化每一路
    for (int i = 0; i < g_cfg.channel_count; i++) {
        ChannelContext *ch = &g_cfg.channels[i];
        // 预先分配一帧，避免运行时爆炸！！！！！！！！！
        ch->frame_buf.virt_addr = (uint8_t *)malloc(1920 * 1080 * 3);
        ch->frame_buf.size = 1920 * 1080 * 3;

        yolov5_init(&ch->yolo_ctx, g_cfg.model_path);
        ch->decoder = gst_decoder_create(ch->rtsp, image_callback, ch);
        gst_decoder_start(ch->decoder);
        g_mutex_init(&ch->frame_lock);
    }

    uint8_t *canvas = (uint8_t *)malloc(CANVAS_W * CANVAS_H * 3);
    gst_display_init(CANVAS_W, CANVAS_H);

    printf("启动成功！\n");
    while (g_running) {
        g_main_context_iteration(NULL, FALSE);
        composite_2x2_rga(canvas, g_cfg.channels, g_cfg.channel_count);
        gst_display_push_rgb(CANVAS_W, CANVAS_H, canvas, CANVAS_W * CANVAS_H * 3);
        g_usleep(16000); 
    }

    // 安全释放
    free(canvas);
    for (int i = 0; i < g_cfg.channel_count; i++) {
        ChannelContext *ch = &g_cfg.channels[i];
        gst_decoder_stop(ch->decoder);
        gst_decoder_destroy(ch->decoder);
        yolov5_release(&ch->yolo_ctx);

        g_mutex_lock(&ch->frame_lock);
        if (ch->frame_buf.virt_addr) {
            free(ch->frame_buf.virt_addr); // 每个只释放一次
        }
        g_mutex_unlock(&ch->frame_lock);
        g_mutex_clear(&ch->frame_lock);
    }

    gst_display_deinit();
    return 0;
}