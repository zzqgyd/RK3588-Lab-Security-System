// ===== 文件: src/yolov5_infer.cc =====
// 零拷贝推理 - 严格按照官方示例架构

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>

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

static void dump_tensor_attr(rknn_tensor_attr *attr)
{
    printf("  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, "
           "zp=%d, scale=%f\n",
           attr->index, attr->name, attr->n_dims, attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
           attr->n_elems, attr->size, get_format_string(attr->fmt), get_type_string(attr->type),
           get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

// ============================================================================
// 公开 API 实现
// ============================================================================

/**
 * @brief 初始化基础模型（全局调用一次）
 */
int yolov5_init_base(base_model_context_t *base_ctx, const char *model_path)
{
    int ret;
    
    if (!base_ctx || !model_path) {
        printf("[yolov5] init_base: invalid params\n");
        return -1;
    }
    
    memset(base_ctx, 0, sizeof(base_model_context_t));
    
    // 1. 初始化 RKNN
    ret = rknn_init(&base_ctx->rknn_ctx, (char *)model_path, 0, 0, NULL);
    if (ret < 0) {
        printf("[yolov5] rknn_init fail! ret=%d\n", ret);
        return -1;
    }
    
    // 2. 查询输入输出数量
    ret = rknn_query(base_ctx->rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &base_ctx->io_num, sizeof(base_ctx->io_num));
    if (ret != RKNN_SUCC) {
        printf("[yolov5] rknn_query IN_OUT_NUM fail! ret=%d\n", ret);
        rknn_destroy(base_ctx->rknn_ctx);
        return -1;
    }
    printf("[yolov5] model input num: %d, output num: %d\n", base_ctx->io_num.n_input, base_ctx->io_num.n_output);
    
    // 3. 获取原生输入属性
    printf("[yolov5] input tensors:\n");
    base_ctx->input_attrs = (rknn_tensor_attr*)malloc(base_ctx->io_num.n_input * sizeof(rknn_tensor_attr));
    memset(base_ctx->input_attrs, 0, base_ctx->io_num.n_input * sizeof(rknn_tensor_attr));
    
    for (int i = 0; i < base_ctx->io_num.n_input; i++) {
        base_ctx->input_attrs[i].index = i;
        ret = rknn_query(base_ctx->rknn_ctx, RKNN_QUERY_NATIVE_INPUT_ATTR, 
                         &base_ctx->input_attrs[i], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            printf("[yolov5] rknn_query NATIVE_INPUT_ATTR fail! ret=%d\n", ret);
            return -1;
        }
        dump_tensor_attr(&base_ctx->input_attrs[i]);
    }
    
    // 4. 获取原生输出属性（零拷贝必须用 NATIVE_NHWC_OUTPUT_ATTR）
    printf("[yolov5] output tensors:\n");
    base_ctx->output_attrs = (rknn_tensor_attr*)malloc(base_ctx->io_num.n_output * sizeof(rknn_tensor_attr));
    memset(base_ctx->output_attrs, 0, base_ctx->io_num.n_output * sizeof(rknn_tensor_attr));
    
    for (int i = 0; i < base_ctx->io_num.n_output; i++) {
        base_ctx->output_attrs[i].index = i;
        ret = rknn_query(base_ctx->rknn_ctx, RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR, 
                         &base_ctx->output_attrs[i], sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            printf("[yolov5] rknn_query NATIVE_NHWC_OUTPUT_ATTR fail! ret=%d\n", ret);
            return -1;
        }
        dump_tensor_attr(&base_ctx->output_attrs[i]);
    }
    
    // 5. 判断量化类型
    if (base_ctx->output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC) {
        base_ctx->is_quant = true;
    } else {
        base_ctx->is_quant = false;
    }
    
    // 6. 解析模型宽高
    if (base_ctx->input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        printf("[yolov5] model is NCHW input fmt\n");
        base_ctx->model_channel = base_ctx->input_attrs[0].dims[1];
        base_ctx->model_height  = base_ctx->input_attrs[0].dims[2];
        base_ctx->model_width   = base_ctx->input_attrs[0].dims[3];
    } else {
        printf("[yolov5] model is NHWC input fmt\n");
        base_ctx->model_height  = base_ctx->input_attrs[0].dims[1];
        base_ctx->model_width   = base_ctx->input_attrs[0].dims[2];
        base_ctx->model_channel = base_ctx->input_attrs[0].dims[3];
    }
    
    printf("[yolov5] model input: height=%d, width=%d, channel=%d, quant=%d\n",
           base_ctx->model_height, base_ctx->model_width, base_ctx->model_channel, base_ctx->is_quant);
    
    // 7. 初始化后处理（加载标签）
    static bool pp_inited = false;
    if (!pp_inited) {
        init_post_process();
        pp_inited = true;
    }
    
    base_ctx->inited = true;
    printf("[yolov5] init_base: success\n");
    
    return 0;
}

/**
 * @brief 复制 Worker 上下文（每个 Worker 独立）
 */
int yolov5_dup_worker_context(base_model_context_t *base_ctx, worker_context_t *worker_ctx, int worker_id)
{
    int ret;
    
    if (!base_ctx || !base_ctx->inited || !worker_ctx) {
        printf("[yolov5] dup_worker: invalid params\n");
        return -1;
    }
    
    memset(worker_ctx, 0, sizeof(worker_context_t));
    worker_ctx->worker_id = worker_id;
    
    // 1. 复制 RKNN 上下文（共享权重）
    ret = rknn_dup_context(&base_ctx->rknn_ctx, &worker_ctx->rknn_ctx);
    if (ret != 0) {
        printf("[yolov5] dup_worker: rknn_dup_context failed, ret=%d\n", ret);
        return -1;
    }
    
    // 2. 绑定专属 NPU 核心
    rknn_core_mask core_mask;
    switch (worker_id % 3) {
        case 0: core_mask = RKNN_NPU_CORE_0; break;
        case 1: core_mask = RKNN_NPU_CORE_1; break;
        case 2: core_mask = RKNN_NPU_CORE_2; break;
        default: core_mask = RKNN_NPU_CORE_0; break;
    }
    
    ret = rknn_set_core_mask(worker_ctx->rknn_ctx, core_mask);
    if (ret != 0) {
        printf("[yolov5] dup_worker: set_core_mask failed (non-fatal)\n");
    } else {
        printf("[yolov5] worker %d bound to NPU core %d\n", worker_id, worker_id % 3);
    }
    
    // 3. 复制模型属性
    worker_ctx->model_channel = base_ctx->model_channel;
    worker_ctx->model_width   = base_ctx->model_width;
    worker_ctx->model_height  = base_ctx->model_height;
    worker_ctx->is_quant      = base_ctx->is_quant;
    worker_ctx->num_outputs   = base_ctx->io_num.n_output;
    
    // 4. 复制输出属性（用于后处理）
    worker_ctx->output_attrs = (rknn_tensor_attr*)malloc(worker_ctx->num_outputs * sizeof(rknn_tensor_attr));
    if (!worker_ctx->output_attrs) {
        printf("[yolov5] dup_worker: malloc output_attrs failed\n");
        rknn_destroy(worker_ctx->rknn_ctx);
        return -1;
    }
    memcpy(worker_ctx->output_attrs, base_ctx->output_attrs, 
           worker_ctx->num_outputs * sizeof(rknn_tensor_attr));
    
    // 5. 设置输入类型和格式（与官方一致）
    rknn_tensor_attr input_attr = base_ctx->input_attrs[0];
    input_attr.type = RKNN_TENSOR_UINT8;
    input_attr.fmt = RKNN_TENSOR_NHWC;
    
    // 6. 创建并绑定输入内存
    worker_ctx->input_mem = rknn_create_mem(worker_ctx->rknn_ctx, input_attr.size_with_stride);
    if (!worker_ctx->input_mem) {
        printf("[yolov5] dup_worker: rknn_create_mem for input failed\n");
        free(worker_ctx->output_attrs);
        rknn_destroy(worker_ctx->rknn_ctx);
        return -1;
    }
    
    ret = rknn_set_io_mem(worker_ctx->rknn_ctx, worker_ctx->input_mem, &input_attr);
    if (ret < 0) {
        printf("[yolov5] dup_worker: rknn_set_io_mem for input failed, ret=%d\n", ret);
        rknn_destroy_mem(worker_ctx->rknn_ctx, worker_ctx->input_mem);
        free(worker_ctx->output_attrs);
        rknn_destroy(worker_ctx->rknn_ctx);
        return -1;
    }
    
    // 7. 创建并绑定输出内存
    for (int i = 0; i < worker_ctx->num_outputs; i++) {
        worker_ctx->output_mems[i] = rknn_create_mem(worker_ctx->rknn_ctx, 
                                                      base_ctx->output_attrs[i].size_with_stride);
        if (!worker_ctx->output_mems[i]) {
            printf("[yolov5] dup_worker: rknn_create_mem for output[%d] failed\n", i);
            // 清理已创建的资源
            rknn_destroy_mem(worker_ctx->rknn_ctx, worker_ctx->input_mem);
            for (int j = 0; j < i; j++) {
                rknn_destroy_mem(worker_ctx->rknn_ctx, worker_ctx->output_mems[j]);
            }
            free(worker_ctx->output_attrs);
            rknn_destroy(worker_ctx->rknn_ctx);
            return -1;
        }
        
        ret = rknn_set_io_mem(worker_ctx->rknn_ctx, worker_ctx->output_mems[i], 
                              &base_ctx->output_attrs[i]);
        if (ret < 0) {
            printf("[yolov5] dup_worker: rknn_set_io_mem for output[%d] failed, ret=%d\n", i, ret);
            rknn_destroy_mem(worker_ctx->rknn_ctx, worker_ctx->input_mem);
            for (int j = 0; j <= i; j++) {
                rknn_destroy_mem(worker_ctx->rknn_ctx, worker_ctx->output_mems[j]);
            }
            free(worker_ctx->output_attrs);
            rknn_destroy(worker_ctx->rknn_ctx);
            return -1;
        }
    }
    
    worker_ctx->inited = true;
    printf("[yolov5] dup_worker[%d]: success\n", worker_id);
    
    return 0;
}

/**
 * @brief 执行推理（严格按照官方示例的参数顺序和内容）
 */
int yolov5_infer(worker_context_t *worker_ctx,
                 image_buffer_t *img,
                 object_detect_result_list *od_results)
{
    int ret;
    image_buffer_t dst_img;
    letterbox_t letter_box;
    const float nms_threshold = NMS_THRESH;
    const float box_conf_threshold = BOX_THRESH;
    const int bg_color = 114;
    
    if (!worker_ctx || !worker_ctx->inited || !img || !od_results) {
        printf("[yolov5] infer: invalid params\n");
        return -1;
    }
    
    memset(od_results, 0, sizeof(*od_results));
    memset(&letter_box, 0, sizeof(letterbox_t));
    memset(&dst_img, 0, sizeof(image_buffer_t));
    
    auto t1 = std::chrono::steady_clock::now();
    
    // ===== 前处理 =====
    dst_img.width  = worker_ctx->model_width;
    dst_img.height = worker_ctx->model_height;
    dst_img.format = IMAGE_FORMAT_RGB888;
    dst_img.size   = get_image_size(&dst_img);
    dst_img.fd     = worker_ctx->input_mem->fd;
    dst_img.virt_addr = (uint8_t *)worker_ctx->input_mem->virt_addr;
    
    if (dst_img.virt_addr == NULL && dst_img.fd == 0) {
        printf("[yolov5] infer: dst_img invalid\n");
        return -1;
    }
    
    ret = convert_image_with_letterbox(img, &dst_img, &letter_box, bg_color);
    if (ret < 0) {
        printf("[yolov5] infer: convert_image_with_letterbox fail! ret=%d\n", ret);
        // 返回 0 而不是 -1，表示没有检测结果，但不崩溃
        return 0;
    }
    
    // 将 letterbox 参数保存到 worker_ctx 中
    worker_ctx->last_letter_box = letter_box;
    
    // 同步输入内存
    rknn_mem_sync(worker_ctx->rknn_ctx, worker_ctx->input_mem, RKNN_MEMORY_SYNC_TO_DEVICE);
    
    auto t2 = std::chrono::steady_clock::now();
    
    // ===== 推理 =====
    ret = rknn_run(worker_ctx->rknn_ctx, NULL);
    if (ret < 0) {
        printf("[yolov5] infer: rknn_run fail! ret=%d\n", ret);
        return -1;
    }
    
    auto t3 = std::chrono::steady_clock::now();
    
    // 同步输出内存
    for (int i = 0; i < worker_ctx->num_outputs; i++) {
        rknn_mem_sync(worker_ctx->rknn_ctx, worker_ctx->output_mems[i], RKNN_MEMORY_SYNC_FROM_DEVICE);
    }
    
    // ===== 后处理（与官方示例参数顺序一致）=====
    // 官方调用: post_process(app_ctx, app_ctx->output_mems, &letter_box, 
    //                        box_conf_threshold, nms_threshold, od_results);
    ret = post_process_with_attrs(
        worker_ctx,                      // 上下文
        worker_ctx->output_mems,         // 输出内存数组
        &worker_ctx->last_letter_box,    // ← 使用保存的 letterbox 参数
        box_conf_threshold,              // 置信度阈值
        nms_threshold,                   // NMS 阈值
        od_results                       // 结果输出
    );
    
    auto t4 = std::chrono::steady_clock::now();
    
    // 性能统计
    static int cnt = 0;
    if (++cnt % 10 == 0) {
        auto rga_us  = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
        auto npu_us  = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
        auto post_us = std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count();
        printf("[Worker %d] RGA:%ldus NPU:%ldus POST:%ldus\n", 
               worker_ctx->worker_id, rga_us, npu_us, post_us);
    }
    
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
    
    if (worker_ctx->output_attrs) {
        free(worker_ctx->output_attrs);
        worker_ctx->output_attrs = NULL;
    }
    
    if (worker_ctx->input_mem) {
        rknn_destroy_mem(worker_ctx->rknn_ctx, worker_ctx->input_mem);
        worker_ctx->input_mem = NULL;
    }
    
    for (int i = 0; i < worker_ctx->num_outputs; i++) {
        if (worker_ctx->output_mems[i]) {
            rknn_destroy_mem(worker_ctx->rknn_ctx, worker_ctx->output_mems[i]);
            worker_ctx->output_mems[i] = NULL;
        }
    }
    
    if (worker_ctx->rknn_ctx) {
        rknn_destroy(worker_ctx->rknn_ctx);
        worker_ctx->rknn_ctx = 0;
    }
    
    worker_ctx->inited = false;
    printf("[yolov5] release_worker[%d]: done\n", worker_ctx->worker_id);
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