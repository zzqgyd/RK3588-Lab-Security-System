// ===== 文件: include/yolov5_infer.h =====
// Step 2 版本：添加 NPU 预分配内存支持

#ifndef __YOLOV5_INFER_H__
#define __YOLOV5_INFER_H__

#include "rknn_api.h"
#include "common.h"
#include "image_utils.h"
#include <stdbool.h>

// ============================================================================
// 基础模型上下文（全局唯一，持有 NPU 权重内存）
// ============================================================================
typedef struct {
    rknn_context rknn_ctx;              // RKNN 基础上下文（持有权重）
    rknn_input_output_num io_num;       // 输入输出数量
    rknn_tensor_attr* input_attrs;      // 输入属性数组
    rknn_tensor_attr* output_attrs;     // 输出属性数组
    
    // ===== Step 2 新增：原生属性（用于零拷贝）=====
    rknn_tensor_attr* native_input_attrs;   // 原生输入属性
    rknn_tensor_attr* native_output_attrs;  // 原生输出属性
    
    int model_channel;                  // 模型输入通道数
    int model_width;                    // 模型输入宽度
    int model_height;                   // 模型输入高度
    bool is_quant;                      // 是否量化模型
    
    bool inited;                        // 初始化标志
} base_model_context_t;

// ============================================================================
// Worker 推理上下文（每个 Worker 线程持有一个）
// ============================================================================
typedef struct worker_context_t{
    rknn_context rknn_ctx;              // 复制的 RKNN 上下文（共享权重）
    int worker_id;                      // 记录绑定的核心 ID  
    int model_channel;                  // 从 base 复制
    int model_width;                    // 从 base 复制
    int model_height;                   // 从 base 复制
    bool is_quant;                      // 从 base 复制
    
    // ===== Step 2 新增：NPU 预分配内存 =====
    rknn_tensor_mem* input_mem;         // NPU 分配的输入内存（含 fd 和 virt_addr）
    rknn_tensor_mem* output_mems[3];    // NPU 分配的输出内存（YOLOv5 固定 3 个输出）
    
    // ===== 输出属性（从 base 复制，用于后处理）=====
    int num_outputs;                    // 输出数量
    rknn_tensor_attr* output_attrs;     // 输出属性数组（动态分配）
    
    // 推理前处理缓冲区（指向 input_mem->virt_addr，Worker 独立）
    image_buffer_t infer_buf;
    
    // LetterBox 参数（每次推理更新）
    letterbox_t last_letter_box;
    
    bool inited;                        // 初始化标志
} worker_context_t;

#include "postprocess.h"

/**
 * @brief 初始化基础模型（全局调用一次）
 * @param base_ctx   [OUT] 基础模型上下文
 * @param model_path [IN]  .rknn 模型文件路径
 * @return 0 成功，-1 失败
 */
int yolov5_init_base(base_model_context_t *base_ctx, const char *model_path);

/**
 * @brief 从基础模型复制一个 Worker 上下文
 * @param base_ctx   [IN]  已初始化的基础模型
 * @param worker_ctx [OUT] Worker 上下文
 * @return 0 成功，-1 失败
 */
int yolov5_dup_worker_context(base_model_context_t *base_ctx, worker_context_t *worker_ctx,int worker_id);

/**
 * @brief 执行推理（Step 2：不再每帧分配内存）
 * @param worker_ctx [IN]  Worker 上下文
 * @param img        [IN]  输入图像（NV12 格式）
 * @param od_results [OUT] 检测结果
 * @return 0 成功，-1 失败
 */
int yolov5_infer(worker_context_t *worker_ctx, 
                 image_buffer_t *img,
                 object_detect_result_list *od_results);

/**
 * @brief 释放 Worker 上下文
 * @param worker_ctx [IN] Worker 上下文
 */
void yolov5_release_worker(worker_context_t *worker_ctx);

/**
 * @brief 释放基础模型
 * @param base_ctx [IN] 基础模型上下文
 */
void yolov5_release_base(base_model_context_t *base_ctx);

#endif