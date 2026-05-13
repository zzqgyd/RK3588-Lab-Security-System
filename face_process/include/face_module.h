#ifndef FACE_MODULE_H
#define FACE_MODULE_H

#include <string>
#include <cstdint>
#include "common.h"

/**
 * @brief 人脸识别结果
 */
struct FaceResult {
    int64_t id;           // 人脸ID，-1表示陌生人
    float similarity;     // 相似度（0.0 ~ 1.0）
    int x, y, w, h;       // 人脸框坐标
    bool valid;           // 是否有效
};

// 识别阈值：相似度低于此值视为陌生人
#define FACE_RECOGNITION_THRESHOLD 0.48f

/**
 * @brief 人脸识别模块
 * 
 * 封装 InspireFace SDK 的初始化、注册、搜索功能。
 * 输入为 image_buffer_t（NV12），与 YOLO 推理用同一格式。
 */
class FaceModule {
public:
    FaceModule();
    ~FaceModule();

    /**
     * @brief 初始化模块
     * @param model_path Gundam_RK3588 模型路径
     * @param db_path    特征库文件路径（.db）
     * @return 0成功，-1失败
     */
    int init(const std::string& model_path, const std::string& db_path);

    /**
     * @brief 注册单张人脸图片
     * @param image_path 图片路径
     * @param out_id     [OUT] 分配的ID
     * @return 0成功，-1失败
     */
    int register_face(const std::string& image_path, int64_t& out_id);

    /**
     * @brief 批量注册文件夹内所有人脸
     * @param folder_path 文件夹路径
     * @return 成功注册的数量
     */
    int register_from_folder(const std::string& folder_path);

    /**
     * @brief 搜索人脸（核心接口）
     * @param img    输入图像（NV12 格式）
     * @param result [OUT] 搜索结果
     * @return 0成功，-1失败
     */
    int search(const image_buffer_t* img, FaceResult& result);

    /*
    * 录入人脸（去重版本）：先搜索再决定是否插入
    * @param feature_id [OUT] SDK 分配/已有的特征 ID
    * @param is_duplicate [OUT] true=重复录入
    * @return 0 成功，-1 失败
    */
    int extract_dedup(const image_buffer_t* img, int& feature_id, bool& is_duplicate);

    /**
     * @brief 释放资源
     */
    void deinit();

private:
    void* session_;   // 隐藏 InspireFace 实现细节
    bool inited_;     // 初始化标志
};

#endif