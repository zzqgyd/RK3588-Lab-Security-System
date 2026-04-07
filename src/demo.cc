#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <gst/gst.h>

#include "gst_decoder.h"
#include "gst_display.h"     // 显示头文件
#include "yolov5_infer.h"
#include "image_drawing.h"

static rknn_app_context_t g_yolo_ctx;
static volatile bool g_running = true;

void sigint_handler(int sig) {
    g_running = false;
    gst_decoder_stop();
    printf("退出信号已接收\n");
}

// 图像回调：推理 + 画框 + 推送显示
void image_callback(int width, int height, const char *format, 
                   void *data, size_t data_size, void *user_data)
{
    if (!g_running) return;

    object_detect_result_list od_results = {0};
    uint8_t *cpu_buf = (uint8_t *)malloc(data_size);
    if (!cpu_buf) return;

    memcpy(cpu_buf, data, data_size);

    image_buffer_t src_img = {
        .width = width,
        .height = height,
        .format = IMAGE_FORMAT_RGB888,
        .virt_addr = cpu_buf,
        .size = data_size
    };

    yolov5_infer(&g_yolo_ctx, &src_img, &od_results);

    // 画框
    for (int i = 0; i < od_results.count; i++) {
        object_detect_result *res = &od_results.results[i];
        int x = res->box.left;
        int y = res->box.top;
        int w = res->box.right - x;
        int h = res->box.bottom - y;

        draw_rectangle(&src_img, x, y, w, h, COLOR_RED, 2);
        char label[64];
        snprintf(label, sizeof(label), "%s %.2f", coco_cls_to_name(res->cls_id), res->prop);
        draw_text(&src_img, label, x, y - 10, COLOR_RED, 12);
    }

    // 推送到屏幕（关键）
    gst_display_push_rgb(width, height, cpu_buf, data_size);

    free(cpu_buf);
}

int main(int argc, char *argv[])
{
    signal(SIGINT, sigint_handler);

    if (argc != 3) {
        printf("用法: %s <模型路径> <RTSP地址>\n", argv[0]);
        return -1;
    }

    gst_init(NULL, NULL);

    if (yolov5_init(&g_yolo_ctx, argv[1]) < 0) {
        printf("模型初始化失败\n");
        return -1;
    }

    if (gst_decoder_init(argv[2], image_callback, NULL) != 0 ||
        gst_decoder_start() != 0) {
        printf("解码器启动失败\n");
        yolov5_release(&g_yolo_ctx);
        return -1;
    }

    printf("运行中，按Ctrl+C退出\n");
    while (g_running)
        g_main_context_iteration(NULL, TRUE);

    // 清理
    gst_decoder_stop();
    gst_decoder_cleanup();
    gst_display_deinit();     // 显示销毁
    yolov5_release(&g_yolo_ctx);
    deinit_post_process();

    printf("退出成功\n");
    return 0;
}