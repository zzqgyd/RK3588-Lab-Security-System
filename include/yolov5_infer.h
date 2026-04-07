#ifndef __YOLOV5_INFER_H__
#define __YOLOV5_INFER_H__

#include "rknn_api.h"
#include "common.h"
#include <stdbool.h>


typedef struct {
    // RKNN 基础信息
    rknn_context rknn_ctx;
    rknn_input_output_num io_num;
    // Tensor 属性
    rknn_tensor_attr* input_attrs;
    rknn_tensor_attr* output_attrs;
    //零拷贝内存
    rknn_tensor_mem*  input_mem;   
    rknn_tensor_mem** output_mem;  
    // 初始化状态
    bool inited;
    // 模型信息
    int model_channel;
    int model_width;
    int model_height;
    bool is_quant;
} rknn_app_context_t;

#include "postprocess.h"


int  yolov5_init(rknn_app_context_t *ctx, const char *model_path);
int yolov5_infer(rknn_app_context_t *ctx,image_buffer_t *img,object_detect_result_list *od_results);
void yolov5_release(rknn_app_context_t *ctx);



#endif