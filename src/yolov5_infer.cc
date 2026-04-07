#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "yolov5_infer.h"

#include "common.h"
#include "file_utils.h"
#include "image_utils.h"

// RKNN & RGA 头文件
#include "rknn_api.h"
#include "im2d.h"
#include "RgaUtils.h"
#include "postprocess.h"

// 宏：检查函数返回值
#define CHECK(ret, func) do { \
    if (ret != 0) { \
        printf("[%s] 失败 ret=%d\n", func, ret); \
        return -1; \
    } \
} while(0)

static void dump_tensor_attr(rknn_tensor_attr *attr)
{
    printf("  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, "
           "zp=%d, scale=%f\n",
           attr->index, attr->name, attr->n_dims, attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
           attr->n_elems, attr->size, get_format_string(attr->fmt), get_type_string(attr->type),
           get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

/*
 * 函数：yolov5_init
 * 功能：初始化 RKNN 模型，创建上下文
 */
int yolov5_init(rknn_app_context_t *ctx, const char *model_path)
{
    int ret = 0;
    memset(ctx, 0, sizeof(rknn_app_context_t));

    // ====================== 1. 读取模型文件 ======================
    int model_len = 0;
    char *model_buf = NULL;
    model_len = read_data_from_file(model_path, &model_buf);
    if (model_buf == NULL) {
        printf("load model fail!\n");
        return -1;
    }

    // ====================== 2. 初始化 RKNN ======================
    ret = rknn_init(&ctx->rknn_ctx, model_buf, model_len, 0, NULL);
    free(model_buf);
    CHECK(ret, "rknn_init");

    // ====================== 3. 查询输入输出数量 ======================
    ret = rknn_query(ctx->rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &ctx->io_num, sizeof(ctx->io_num));
    CHECK(ret, "rknn_query_io_num");
    printf("模型输入张量数: %d, 输出张量数: %d\n", ctx->io_num.n_input, ctx->io_num.n_output);

    // ====================== 4. 申请属性内存 ======================
    ctx->input_attrs  = (rknn_tensor_attr*)malloc(ctx->io_num.n_input  * sizeof(rknn_tensor_attr));
    ctx->output_attrs = (rknn_tensor_attr*)malloc(ctx->io_num.n_output * sizeof(rknn_tensor_attr));
    if (!ctx->input_attrs || !ctx->output_attrs) {
        printf("申请属性内存失败！\n");
        return -1;
    }

    // ====================== 5. 获取输入属性 ======================
    for (int i = 0; i < ctx->io_num.n_input; i++) {
        ctx->input_attrs[i].index = i;
        ret = rknn_query(ctx->rknn_ctx, RKNN_QUERY_INPUT_ATTR, &ctx->input_attrs[i], sizeof(rknn_tensor_attr));
        CHECK(ret, "获取输入属性");
        dump_tensor_attr(&ctx->input_attrs[i]);
    }

    // ====================== 6. 获取输出属性 ======================
    for (int i = 0; i < ctx->io_num.n_output; i++) {
        ctx->output_attrs[i].index = i;
        ret = rknn_query(ctx->rknn_ctx, RKNN_QUERY_OUTPUT_ATTR, &ctx->output_attrs[i], sizeof(rknn_tensor_attr));
        CHECK(ret, "获取输出属性");
        dump_tensor_attr(&ctx->output_attrs[i]);
    }

    // ====================== 7. 判断是否量化 ======================
    ctx->is_quant = false;
    if (ctx->output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC &&
        ctx->output_attrs[0].type != RKNN_TENSOR_FLOAT16) {
        ctx->is_quant = true;
    }

    // ====================== 8. 解析模型宽高 ======================
    if (ctx->input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        ctx->model_channel = ctx->input_attrs[0].dims[1];
        ctx->model_height  = ctx->input_attrs[0].dims[2];
        ctx->model_width   = ctx->input_attrs[0].dims[3];
    } else {
        ctx->model_height  = ctx->input_attrs[0].dims[1];
        ctx->model_width   = ctx->input_attrs[0].dims[2];
        ctx->model_channel = ctx->input_attrs[0].dims[3];
    }

    printf("model input: H=%d W=%d C=%d\n", ctx->model_height, ctx->model_width, ctx->model_channel);

    // 初始化标签
    init_post_process();
    ctx->inited = true;
    return 0;
}

/*
 * 函数：yolov5_infer
 * 功能：完全对齐官方RKNN流程
 */
int yolov5_infer(rknn_app_context_t *ctx,
                           image_buffer_t *img,
                           object_detect_result_list *od_results)
{
    int ret;
    letterbox_t letter_box;
    image_buffer_t dst_img;
    rknn_input inputs[ctx->io_num.n_input];
    rknn_output outputs[ctx->io_num.n_output];

    const float nms_threshold  = NMS_THRESH;
    const float box_conf_threshold = BOX_THRESH;
    int bg_color = 114;

    if (!ctx || !img || !od_results) return -1;

    memset(od_results, 0, sizeof(object_detect_result_list));
    memset(&letter_box, 0, sizeof(letterbox_t));
    memset(&dst_img, 0, sizeof(image_buffer_t));
    memset(inputs, 0, sizeof(inputs));
    memset(outputs, 0, sizeof(outputs));

    // 分配模型输入图像内存
    dst_img.width   = ctx->model_width;
    dst_img.height  = ctx->model_height;
    dst_img.format  = IMAGE_FORMAT_RGB888;
    dst_img.size    = get_image_size(&dst_img);
    dst_img.virt_addr = (unsigned char*)malloc(dst_img.size);
    if (!dst_img.virt_addr) {
        printf("malloc dst_img fail!\n");
        return -1;
    }

    // letterbox 缩放
    ret = convert_image_with_letterbox(img, &dst_img, &letter_box, bg_color);
    if (ret < 0) {
        printf("convert_image_with_letterbox failed!\n");
        free(dst_img.virt_addr);
        return -1;
    }

    // ======================
    // RKNN 官方标准输入设置
    // ======================
    inputs[0].index = 0;
    inputs[0].type  = RKNN_TENSOR_UINT8;
    inputs[0].fmt   = RKNN_TENSOR_NHWC;
    inputs[0].size  = dst_img.size;
    inputs[0].buf   = dst_img.virt_addr;

    ret = rknn_inputs_set(ctx->rknn_ctx, ctx->io_num.n_input, inputs);
    if (ret < 0) {
        printf("rknn_inputs_set fail!\n");
        free(dst_img.virt_addr);
        return -1;
    }

    // 推理
    ret = rknn_run(ctx->rknn_ctx, NULL);

    // 获取输出
    for (int i = 0; i < ctx->io_num.n_output; i++) {
        outputs[i].index = i;
        outputs[i].want_float = !ctx->is_quant;
    }

    ret = rknn_outputs_get(ctx->rknn_ctx, ctx->io_num.n_output, outputs, NULL);
    if (ret < 0) {
        printf("rknn_outputs_get fail!\n");
        free(dst_img.virt_addr);
        return -1;
    }

    // 后处理
    post_process(ctx, outputs, &letter_box, box_conf_threshold, nms_threshold, od_results);

    // 释放输出
    rknn_outputs_release(ctx->rknn_ctx, ctx->io_num.n_output, outputs);

    // 释放内存
    free(dst_img.virt_addr);

    return 0;
}

/*
 * 函数：yolov5_release
 */
void yolov5_release(rknn_app_context_t *ctx)
{
    if (!ctx->inited) return;

    // 释放资源
    free(ctx->input_attrs);
    free(ctx->output_attrs);
    rknn_destroy(ctx->rknn_ctx);

    ctx->inited = false;
    printf("yolov5 release!\n");
}