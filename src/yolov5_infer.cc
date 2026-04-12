#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "yolov5_infer.h"

#include "common.h"
#include "file_utils.h"
#include "image_utils.h"
#include "postprocess.h"

// RKNN & RGA 头文件
#include "rknn_api.h"
#include "im2d.h"
#include "RgaUtils.h"

// ============================================================================
// 内部辅助函数
// ============================================================================

/**
 * @brief 查询并填充模型属性
 */
static int query_model_attributes(rknn_context rknn_ctx,
                                   rknn_input_output_num *io_num,
                                   rknn_tensor_attr **input_attrs_ptr,
                                   rknn_tensor_attr **output_attrs_ptr,
                                   int *model_channel,
                                   int *model_width,
                                   int *model_height,
                                   bool *is_quant)
{
    int ret;
    rknn_tensor_attr *input_attrs = NULL;
    rknn_tensor_attr *output_attrs = NULL;
    
    // 1. 查询输入输出数量
    ret = rknn_query(rknn_ctx, RKNN_QUERY_IN_OUT_NUM, io_num, sizeof(*io_num));
    if (ret != 0) {
        printf("[yolov5] rknn_query IN_OUT_NUM failed, ret=%d\n", ret);
        return -1;
    }
    
    // 2. 分配属性内存
    input_attrs = (rknn_tensor_attr*)malloc(io_num->n_input * sizeof(rknn_tensor_attr));
    output_attrs = (rknn_tensor_attr*)malloc(io_num->n_output * sizeof(rknn_tensor_attr));
    if (!input_attrs || !output_attrs) {
        printf("[yolov5] malloc attrs failed\n");
        free(input_attrs);
        free(output_attrs);
        return -1;
    }
    
    // 3. 获取输入属性
    for (int i = 0; i < io_num->n_input; i++) {
        input_attrs[i].index = i;
        ret = rknn_query(rknn_ctx, RKNN_QUERY_INPUT_ATTR, &input_attrs[i], sizeof(rknn_tensor_attr));
        if (ret != 0) {
            printf("[yolov5] rknn_query INPUT_ATTR[%d] failed\n", i);
            free(input_attrs);
            free(output_attrs);
            return -1;
        }
    }
    
    // 4. 获取输出属性
    for (int i = 0; i < io_num->n_output; i++) {
        output_attrs[i].index = i;
        ret = rknn_query(rknn_ctx, RKNN_QUERY_OUTPUT_ATTR, &output_attrs[i], sizeof(rknn_tensor_attr));
        if (ret != 0) {
            printf("[yolov5] rknn_query OUTPUT_ATTR[%d] failed\n", i);
            free(input_attrs);
            free(output_attrs);
            return -1;
        }
    }
    
    // 5. 判断量化
    *is_quant = false;
    if (output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC &&
        output_attrs[0].type != RKNN_TENSOR_FLOAT16) {
        *is_quant = true;
    }
    
    // 6. 解析模型宽高
    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        *model_channel = input_attrs[0].dims[1];
        *model_height  = input_attrs[0].dims[2];
        *model_width   = input_attrs[0].dims[3];
    } else {
        *model_height  = input_attrs[0].dims[1];
        *model_width   = input_attrs[0].dims[2];
        *model_channel = input_attrs[0].dims[3];
    }
    
    *input_attrs_ptr = input_attrs;
    *output_attrs_ptr = output_attrs;
    
    printf("[yolov5] Model: %dx%d, channel=%d, quant=%d, inputs=%d, outputs=%d\n", 
           *model_width, *model_height, *model_channel, *is_quant,
           io_num->n_input, io_num->n_output);
    
    return 0;
}

// ============================================================================
// 公开 API 实现
// ============================================================================

/**
 * @brief 初始化基础模型
 */
int yolov5_init_base(base_model_context_t *base_ctx, const char *model_path)
{
    int ret;
    int model_len = 0;
    char *model_buf = NULL;
    
    if (!base_ctx || !model_path) {
        printf("[yolov5] init_base: invalid params\n");
        return -1;
    }
    
    memset(base_ctx, 0, sizeof(base_model_context_t));
    
    // 1. 读取模型文件
    model_len = read_data_from_file(model_path, &model_buf);
    if (model_buf == NULL) {
        printf("[yolov5] init_base: read model file failed: %s\n", model_path);
        return -1;
    }
    
    // 2. 初始化 RKNN（权重加载到 NPU 内存，全局唯一）
    ret = rknn_init(&base_ctx->rknn_ctx, model_buf, model_len, 0, NULL);
    free(model_buf);
    if (ret != 0) {
        printf("[yolov5] init_base: rknn_init failed, ret=%d\n", ret);
        return -1;
    }
    
    // 3. 设置 NPU 核心（使用全部 3 核）
    rknn_core_mask core_mask = RKNN_NPU_CORE_ALL;
    ret = rknn_set_core_mask(base_ctx->rknn_ctx, core_mask);
    if (ret != 0) {
        printf("[yolov5] init_base: rknn_set_core_mask failed, ret=%d (non-fatal)\n", ret);
    }
    
    // 4. 查询模型属性
    ret = query_model_attributes(base_ctx->rknn_ctx,
                                  &base_ctx->io_num,
                                  &base_ctx->input_attrs,
                                  &base_ctx->output_attrs,
                                  &base_ctx->model_channel,
                                  &base_ctx->model_width,
                                  &base_ctx->model_height,
                                  &base_ctx->is_quant);
    if (ret != 0) {
        printf("[yolov5] init_base: query_model_attributes failed\n");
        rknn_destroy(base_ctx->rknn_ctx);
        return -1;
    }
    
    base_ctx->inited = true;
    printf("[yolov5] init_base: success, model=%s\n", model_path);
    
    return 0;
}

/**
 * @brief 复制 Worker 上下文
 */
int yolov5_dup_worker_context(base_model_context_t *base_ctx, worker_context_t *worker_ctx)
{
    int ret;
    int buf_size;
    
    if (!base_ctx || !base_ctx->inited || !worker_ctx) {
        printf("[yolov5] dup_worker: invalid params\n");
        return -1;
    }
    
    memset(worker_ctx, 0, sizeof(worker_context_t));
    
    // 1. 复制 RKNN 上下文（共享权重，只创建新推理实例）
    ret = rknn_dup_context(&base_ctx->rknn_ctx,&worker_ctx->rknn_ctx);
    if (ret != 0) {
        printf("[yolov5] dup_worker: rknn_dup_context failed, ret=%d\n", ret);
        return -1;
    }
    
    // 2. 复制模型属性（只读）
    worker_ctx->model_channel = base_ctx->model_channel;
    worker_ctx->model_width   = base_ctx->model_width;
    worker_ctx->model_height  = base_ctx->model_height;
    worker_ctx->is_quant      = base_ctx->is_quant;
    
    // ===== 3. 复制输出属性（用于后处理）=====
    worker_ctx->num_outputs = base_ctx->io_num.n_output;
    worker_ctx->output_attrs = (rknn_tensor_attr*)malloc(
        worker_ctx->num_outputs * sizeof(rknn_tensor_attr)
    );
    if (!worker_ctx->output_attrs) {
        printf("[yolov5] dup_worker: malloc output_attrs failed\n");
        rknn_destroy(worker_ctx->rknn_ctx);
        return -1;
    }
    memcpy(worker_ctx->output_attrs, base_ctx->output_attrs,
           worker_ctx->num_outputs * sizeof(rknn_tensor_attr));
    
    // 4. 分配 Worker 独立的推理前处理缓冲区
    buf_size = worker_ctx->model_width * worker_ctx->model_height * 3;  // RGB888
    worker_ctx->infer_buf.virt_addr = (uint8_t*)malloc(buf_size);
    if (!worker_ctx->infer_buf.virt_addr) {
        printf("[yolov5] dup_worker: malloc infer_buf failed, size=%d\n", buf_size);
        free(worker_ctx->output_attrs);
        rknn_destroy(worker_ctx->rknn_ctx);
        return -1;
    }
    worker_ctx->infer_buf.size   = buf_size;
    worker_ctx->infer_buf.width  = worker_ctx->model_width;
    worker_ctx->infer_buf.height = worker_ctx->model_height;
    worker_ctx->infer_buf.format = IMAGE_FORMAT_RGB888;
    
    worker_ctx->inited = true;
    printf("[yolov5] dup_worker: success\n");
    
    return 0;
}

/**
 * @brief 执行推理
 */
int yolov5_infer(worker_context_t *worker_ctx,
                 image_buffer_t *img,
                 object_detect_result_list *od_results)
{
    int ret;
    rknn_input inputs[1];
    rknn_output outputs[3];  // YOLOv5 固定 3 个输出
    
    const float nms_threshold  = NMS_THRESH;
    const float box_conf_threshold = BOX_THRESH;
    const int bg_color = 114;
    
    if (!worker_ctx || !worker_ctx->inited || !img || !od_results) {
        printf("[yolov5] infer: invalid params\n");
        return -1;
    }
    
    memset(od_results, 0, sizeof(object_detect_result_list));
    memset(inputs, 0, sizeof(inputs));
    memset(outputs, 0, sizeof(outputs));
    memset(&worker_ctx->last_letter_box, 0, sizeof(letterbox_t));
    
    // 1. 前处理：letterbox 缩放 + NV12→RGB 转换（RGA 硬件加速）
    image_buffer_t *dst_img = &worker_ctx->infer_buf;
    ret = convert_image_with_letterbox(img, dst_img, &worker_ctx->last_letter_box, bg_color);
    if (ret < 0) {
        printf("[yolov5] infer: convert_image_with_letterbox failed\n");
        return -1;
    }
    
    // 2. 设置输入
    inputs[0].index = 0;
    inputs[0].type  = RKNN_TENSOR_UINT8;
    inputs[0].fmt   = RKNN_TENSOR_NHWC;
    inputs[0].size  = dst_img->size;
    inputs[0].buf   = dst_img->virt_addr;
    
    ret = rknn_inputs_set(worker_ctx->rknn_ctx, 1, inputs);
    if (ret < 0) {
        printf("[yolov5] infer: rknn_inputs_set failed, ret=%d\n", ret);
        return -1;
    }
    
    // 3. 推理
    ret = rknn_run(worker_ctx->rknn_ctx, NULL);
    if (ret < 0) {
        printf("[yolov5] infer: rknn_run failed, ret=%d\n", ret);
        return -1;
    }
    
    // 4. 获取输出
    for (int i = 0; i < 3; i++) {
        outputs[i].index = i;
        outputs[i].want_float = !worker_ctx->is_quant;
    }
    
    ret = rknn_outputs_get(worker_ctx->rknn_ctx, 3, outputs, NULL);
    if (ret < 0) {
        printf("[yolov5] infer: rknn_outputs_get failed, ret=%d\n", ret);
        return -1;
    }
    
    // 5. 后处理（传入 Worker 的属性）
    ret = post_process_with_attrs(
        worker_ctx,
        outputs,
        &worker_ctx->last_letter_box,
        box_conf_threshold,
        nms_threshold,
        od_results
    );
    
    // 6. 释放输出
    rknn_outputs_release(worker_ctx->rknn_ctx, 3, outputs);
    
    return ret;
}

/**
 * @brief 释放 Worker 上下文
 */
void yolov5_release_worker(worker_context_t *worker_ctx)
{
    if (!worker_ctx || !worker_ctx->inited) {
        return;
    }
    
    if (worker_ctx->infer_buf.virt_addr) {
        free(worker_ctx->infer_buf.virt_addr);
        worker_ctx->infer_buf.virt_addr = NULL;
    }
    
    if (worker_ctx->rknn_ctx) {
        rknn_destroy(worker_ctx->rknn_ctx);
        worker_ctx->rknn_ctx = 0;
    }
    
    worker_ctx->inited = false;
    printf("[yolov5] release_worker: done\n");
}

/**
 * @brief 释放基础模型
 */
void yolov5_release_base(base_model_context_t *base_ctx)
{
    if (!base_ctx || !base_ctx->inited) {
        return;
    }
    
    if (base_ctx->input_attrs) {
        free(base_ctx->input_attrs);
        base_ctx->input_attrs = NULL;
    }
    
    if (base_ctx->output_attrs) {
        free(base_ctx->output_attrs);
        base_ctx->output_attrs = NULL;
    }
    
    if (base_ctx->rknn_ctx) {
        rknn_destroy(base_ctx->rknn_ctx);
        base_ctx->rknn_ctx = 0;
    }
    
    base_ctx->inited = false;
    printf("[yolov5] release_base: done\n");
}