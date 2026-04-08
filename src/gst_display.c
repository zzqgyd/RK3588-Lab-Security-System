#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include "gst_display.h"

static GstElement *g_display_pipeline = NULL;
static GstElement *g_display_appsrc = NULL;

int gst_display_init(int width, int height)
{
    GError *error = NULL;
    gchar *pipe_str = g_strdup_printf(
        "appsrc name=display_appsrc is-live=true format=GST_FORMAT_TIME ! "
        "video/x-raw,format=RGB,width=%d,height=%d,framerate=30/1 ! "
        "videoconvert ! videoscale ! autovideosink sync=false async=false",
        width, height
    );

    g_display_pipeline = gst_parse_launch(pipe_str, &error);
    g_free(pipe_str);

    if (!g_display_pipeline) {
        g_printerr("display pipeline failed: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    g_display_appsrc = gst_bin_get_by_name(GST_BIN(g_display_pipeline), "display_appsrc");
    if (!g_display_appsrc) {
        g_printerr("appsrc not found\n");
        gst_object_unref(g_display_pipeline);
        g_display_pipeline = NULL;
        return -1;
    }

    gst_element_set_state(g_display_pipeline, GST_STATE_PLAYING);
    return 0;
}

void gst_display_push_rgb(int width, int height, uint8_t *rgb_data, size_t size)
{
    if (!g_display_appsrc) {
        if (gst_display_init(width, height) < 0)
            return;
    }

    // 关键修复：创建缓冲区时拷贝数据，避免外部内存被释放
    GstBuffer *buf = gst_buffer_new_allocate(NULL, size, NULL);
    if (!buf) return;

    GstMapInfo map;
    if (gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
        memcpy(map.data, rgb_data, size);
        gst_buffer_unmap(buf, &map);
    }

    gst_app_src_push_buffer(GST_APP_SRC(g_display_appsrc), buf);
}

void gst_display_deinit(void)
{
    if (g_display_appsrc) {
        gst_object_unref(g_display_appsrc);
        g_display_appsrc = NULL;
    }
    if (g_display_pipeline) {
        gst_element_set_state(g_display_pipeline, GST_STATE_NULL);
        gst_object_unref(g_display_pipeline);
        g_display_pipeline = NULL;
    }
}