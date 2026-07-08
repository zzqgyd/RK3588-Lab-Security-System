#include "face_module.h"
#include "common.h"  // image_buffer_t 定义在这里
#include <iostream>
#include <dirent.h>
#include <cstring>
#include <inspireface/inspireface.hpp>
#include <inspirecv/inspirecv.h>
#include "im2d.h" 

FaceModule::FaceModule() : session_(nullptr), inited_(false) {}

FaceModule::~FaceModule() {
    deinit();
}

// ================================================================
// FaceModule::init — 初始化人脸识别模块
// ================================================================
int FaceModule::init(const std::string& model_path, const std::string& db_path) {
    if (inited_) return 0;                                    // 已初始化，跳过

    // ============ 1. 加载模型 ============
    INSPIREFACE_CONTEXT->Reload(model_path);                 // 加载人脸识别模型文件
    if (!INSPIREFACE_CONTEXT->isMLoad()) {                   // 检查是否加载成功
        std::cerr << "[FaceModule] Model load failed: " << model_path << std::endl;
        return -1;
    }

    // ============ 2. 开启特征库（InspireFace 自带的 SQLite 持久化） ============
    inspire::DatabaseConfiguration db_config;
    db_config.enable_persistence = true;                     // 开启持久化（关闭后数据不丢失）
    db_config.persistence_db_path = db_path;                 // 特征库文件路径
    db_config.search_mode = inspire::SEARCH_MODE_EXHAUSTIVE; // 搜索模式：全量搜索（最准确）
    db_config.recognition_threshold = FACE_RECOGNITION_THRESHOLD; // 识别阈值（低于此值视为不认识）
    db_config.primary_key_mode = inspire::AUTO_INCREMENT;    // 主键模式：自动递增分配 feature_id

    auto ret = INSPIREFACE_FEATURE_HUB->EnableHub(db_config); // 启用特征中心
    if (ret != HSUCCEED) {
        std::cerr << "[FaceModule] EnableHub failed: " << ret << std::endl;
        return -1;
    }

    // ============ 3. 创建会话（人脸检测 + 特征提取的流水线） ============
    auto param = inspire::CustomPipelineParameter();
    param.enable_recognition = true;                         // 开启识别功能

    auto session_ptr = inspire::Session::CreatePtr(
        inspire::DETECT_MODE_ALWAYS_DETECT,                  // 检测模式：每帧都检测人脸
        5,                                                   // 最大跟踪人脸数
        param,                                               // 自定义参数
        320                                                  // 人脸检测最小尺寸（像素）
    );
    if (!session_ptr) {
        std::cerr << "[FaceModule] Create session failed" << std::endl;
        return -1;
    }

    // 用 void* 保存 Session，避免头文件暴露 InspireFace 类型
    session_ = new std::shared_ptr<inspire::Session>(session_ptr);
    inited_ = true;

    std::cout << "[FaceModule] Init success" << std::endl;
    return 0;
}


int FaceModule::register_face(const std::string& image_path, int64_t& out_id) {
    if (!inited_) return -1;

    auto session = *(std::shared_ptr<inspire::Session>*)session_;

    // 加载注册图片
    auto reg_image = inspirecv::Image::Create(image_path);
    if (!reg_image.Data()) {
        std::cerr << "[FaceModule] Cannot load image: " << image_path << std::endl;
        return -1;
    }

    auto reg_process = inspirecv::FrameProcess::Create(
        reg_image.Data(), reg_image.Height(), reg_image.Width(),
        inspirecv::BGR, inspirecv::ROTATION_0
    );

    // 检测人脸
    std::vector<inspire::FaceTrackWrap> results;
    session->FaceDetectAndTrack(reg_process, results);

    if (results.empty()) {
        std::cerr << "[FaceModule] No face found in: " << image_path << std::endl;
        return -1;
    }

    // 提取特征
    inspire::FaceEmbedding feature;
    session->FaceFeatureExtract(reg_process, results[0], feature);

    // 插入特征库
    auto ret = INSPIREFACE_FEATURE_HUB->FaceFeatureInsert(
        feature.embedding, INSPIRE_INVALID_ID, out_id
    );

    if (ret == HSUCCEED) {
        std::cout << "[FaceModule] Registered: " << image_path << " -> ID:" << out_id << std::endl;
        return 0;
    }
    return -1;
}

int FaceModule::register_from_folder(const std::string& folder_path) {
    int count = 0;
    DIR* dir = opendir(folder_path.c_str());
    if (!dir) {
        std::cerr << "[FaceModule] Cannot open folder: " << folder_path << std::endl;
        return -1;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename.length() < 4) continue;

        // 只处理 jpg/png
        std::string ext = filename.substr(filename.length() - 4);
        if (ext != ".jpg" && ext != ".png" && ext != ".JPG" && ext != ".PNG") continue;

        std::string full_path = folder_path + "/" + filename;
        int64_t id;
        if (register_face(full_path, id) == 0) {
            count++;
        }
    }
    closedir(dir);
    std::cout << "[FaceModule] Registered " << count << " faces from folder" << std::endl;
    return count;
}

// ================================================================
// FaceModule::search — 人脸搜索（识别人脸，返回是谁）
// 参数：
//   img：摄像头采集的 YUYV 图像（virt_addr + fd + 宽高）
//   result：[OUT] 识别结果（是否有效、坐标、feature_id、相似度）
// 返回：0 成功，-1 失败
// ================================================================
int FaceModule::search(const image_buffer_t* img, FaceResult& result) {
    if (!inited_ || !img || !img->virt_addr) {               // 参数安全检查
        result.valid = false;
        return -1;
    }

    // ★ 加锁：防止 qt_command_handler 的 extract_dedup 并发访问 Session/RGA
    std::lock_guard<std::mutex> lk(mutex_);

    auto session = *(std::shared_ptr<inspire::Session>*)session_; // 取出 Session

    // ============ 1. YUYV → BGR 转换（RGA 硬件加速） ============
    int bgr_size = img->width * img->height * 3;             // BGR 每像素 3 字节
    uint8_t* bgr_buffer = (uint8_t*)malloc(bgr_size);        // 分配 BGR 临时缓冲区
    if (!bgr_buffer) {
        result.valid = false;
        return -1;
    }

    rga_buffer_t src;                                        // RGA 输入：YUYV 图像
    if (img->fd >= 0) {                                      // 优先用 DMA-BUF fd（零拷贝）
        src = wrapbuffer_fd(img->fd, img->width, img->height,
                            RK_FORMAT_YUYV_422,
                            img->width, img->height);
    } else {                                                 // 否则用虚拟地址
        src = wrapbuffer_virtualaddr((void*)img->virt_addr, img->width, img->height,
                                     RK_FORMAT_YUYV_422,
                                     img->width, img->height);
    }

    rga_buffer_t dst = wrapbuffer_virtualaddr(bgr_buffer, img->width, img->height,
                                               RK_FORMAT_BGR_888); // RGA 输出：BGR

    im_rect src_rect = {0, 0, img->width, img->height};     // 源区域：整幅图
    im_rect dst_rect = {0, 0, img->width, img->height};     // 目标区域：整幅图

    // imcheck：提交前校验 buffer 参数，避免 RGA 驱动 "Invalid argument" 导致的崩溃
    IM_STATUS check_ret = imcheck(src, dst, src_rect, dst_rect);
    if (check_ret != IM_STATUS_NOERROR) {
        printf("[FaceModule] RGA imcheck failed: %d\n", (int)check_ret);
        free(bgr_buffer);
        result.valid = false;
        return -1;  // RGA 参数无效属于失败，返回 -1 而非 0
    }

    if (improcess(src, dst, {}, src_rect, dst_rect, {}, 0) != IM_STATUS_SUCCESS) {
        printf("[FaceModule] RGA YUYV→BGR failed\n");
        free(bgr_buffer);
        result.valid = false;
        return -1;  // 修复：RGA 转换失败应返回 -1，原 return 0 会被调用方误判为成功
    }

    // ============ 2. 创建 InspireFace 帧处理对象 ============
    auto image_process = inspirecv::FrameProcess::Create(
        bgr_buffer,                                          // BGR 数据
        img->height, img->width,                             // 图像尺寸
        inspirecv::BGR,                                      // 颜色格式
        inspirecv::ROTATION_0                                // 不旋转
    );

    // ============ 3. 人脸检测 ============
    std::vector<inspire::FaceTrackWrap> faces;
    session->FaceDetectAndTrack(image_process, faces);       // 检测 + 跟踪所有人脸

    if (faces.empty()) {                                     // 没检测到人脸
        free(bgr_buffer);
        result.valid = false;
        return 0;                                            // 返回0表示正常，但没找到脸
    }

    // ============ 4. 取第一个人脸的信息 ============
    auto& face = faces[0];
    auto rect = session->GetFaceBoundingBox(face);           // 获取人脸框坐标
    result.x = rect.GetX();                                  // 左上角 X
    result.y = rect.GetY();                                  // 左上角 Y
    result.w = rect.GetWidth();                              // 框宽度
    result.h = rect.GetHeight();                             // 框高度

    // ============ 5. 提取特征 + 搜索 ============
    inspire::FaceEmbedding feature;
    session->FaceFeatureExtract(image_process, face, feature); // 提取特征向量

    inspire::FaceSearchResult search_result;
    INSPIREFACE_FEATURE_HUB->SearchFaceFeature(              // 在特征库中搜索
        feature.embedding,                                   // 输入：提取到的特征向量
        search_result,                                       // 输出：搜索结果
        false                                                
    );

    // ============ 6. 填充结果 ============
    result.id = search_result.id;                            // 特征ID
    result.similarity = search_result.similarity;            // 相似度（0~1）
    result.valid = (result.id != INSPIRE_INVALID_ID          // 有效：ID有效 且 相似度超阈值
                    && result.similarity > FACE_RECOGNITION_THRESHOLD);

    free(bgr_buffer);
    return 0;
}

// ================================================================
// FaceModule::search_bgr — BGR 直传搜索（跳过 YUYV→BGR RGA 转换）
// ----------------------------------------------------------------
// 用于 ESP32 RTSP/本地视频流识别：OpenCV 解码得到 BGR Mat 后直接调用此接口。
// ================================================================
int FaceModule::search_bgr(const uint8_t* bgr_data, int width, int height, FaceResult& result)
{
    if (!inited_ || !bgr_data || width <= 0 || height <= 0) {
        result.valid = false;
        return -1;
    }

    // ★ 加锁：与 search/extract_dedup 互斥，InspireFace Session 非线程安全
    std::lock_guard<std::mutex> lk(mutex_);
    auto session = *(std::shared_ptr<inspire::Session>*)session_;

    // 直接用 BGR 数据创建 FrameProcess（无需 RGA 转换）
    auto image_process = inspirecv::FrameProcess::Create(
        bgr_data,
        height, width,
        inspirecv::BGR,
        inspirecv::ROTATION_0
    );

    // 人脸检测
    std::vector<inspire::FaceTrackWrap> faces;
    session->FaceDetectAndTrack(image_process, faces);

    if (faces.empty()) {
        result.valid = false;
        return 0;
    }

    // 取第一个人脸
    auto& face = faces[0];
    auto rect = session->GetFaceBoundingBox(face);
    result.x = rect.GetX();
    result.y = rect.GetY();
    result.w = rect.GetWidth();
    result.h = rect.GetHeight();

    // 提取特征 + 搜索
    inspire::FaceEmbedding feature;
    session->FaceFeatureExtract(image_process, face, feature);

    inspire::FaceSearchResult search_result;
    INSPIREFACE_FEATURE_HUB->SearchFaceFeature(
        feature.embedding,
        search_result,
        false
    );

    result.id = search_result.id;
    result.similarity = search_result.similarity;
    result.valid = (result.id != INSPIRE_INVALID_ID
                    && result.similarity > FACE_RECOGNITION_THRESHOLD);

    return 0;
}

// ================================================================
// FaceModule::extract_dedup — 人脸录入去重
// 功能：提取特征 → 先搜索是否已存在 → 已存在返回已有ID
//       不存在则插入新特征，返回新ID
// 参数：
//   img：摄像头采集的 YUYV 图像
//   feature_id：[OUT] 特征ID（已有或新分配的）
//   is_duplicate：[OUT] true=已存在此人，false=新人
// 返回：0 成功，-1 失败
// ================================================================
int FaceModule::extract_dedup(const image_buffer_t* img, int& feature_id, bool& is_duplicate) {
    if (!inited_ || !img || !img->virt_addr) return -1;
    is_duplicate = false;

    // ★ 加锁：防止 main_event_handler 的 search 并发访问 Session/RGA
    std::lock_guard<std::mutex> lk(mutex_);

    auto session = *(std::shared_ptr<inspire::Session>*)session_;

    // ============ 1. YUYV → BGR（同 search，RGA 硬件转换） ============
    int bgr_size = img->width * img->height * 3;
    uint8_t* bgr_buffer = (uint8_t*)malloc(bgr_size);
    if (!bgr_buffer) return -1;

    rga_buffer_t src;
    if (img->fd >= 0) {
        src = wrapbuffer_fd(img->fd, img->width, img->height,
                            RK_FORMAT_YUYV_422, img->width, img->height);
    } else {
        src = wrapbuffer_virtualaddr((void*)img->virt_addr, img->width, img->height,
                                     RK_FORMAT_YUYV_422, img->width, img->height);
    }
    rga_buffer_t dst = wrapbuffer_virtualaddr(bgr_buffer, img->width, img->height,
                                               RK_FORMAT_BGR_888);
    im_rect src_rect = {0, 0, img->width, img->height};
    im_rect dst_rect = {0, 0, img->width, img->height};

    // imcheck：提交前校验 buffer 参数，避免 RGA 驱动 "Invalid argument" 导致的崩溃
    IM_STATUS check_ret = imcheck(src, dst, src_rect, dst_rect);
    if (check_ret != IM_STATUS_NOERROR) {
        printf("[FaceModule] RGA imcheck failed: %d\n", (int)check_ret);
        free(bgr_buffer);
        return -1;  // RGA 参数无效属于失败，返回 -1
    }

    if (improcess(src, dst, {}, src_rect, dst_rect, {}, 0) != IM_STATUS_SUCCESS) {
        printf("[FaceModule] RGA YUYV→BGR failed\n");
        free(bgr_buffer);
        return -1;  // 修复：原 return 0 会导致调用方使用未初始化的 feature_id
    }

    // ============ 2. 人脸检测 ============
    auto image_process = inspirecv::FrameProcess::Create(
        bgr_buffer, img->height, img->width,
        inspirecv::BGR, inspirecv::ROTATION_0);

    std::vector<inspire::FaceTrackWrap> faces;
    session->FaceDetectAndTrack(image_process, faces);
    if (faces.empty()) {                                     // 没检测到脸
        free(bgr_buffer);
        return -1;
    }

    // ============ 3. 提取特征 ============
    inspire::FaceEmbedding feature;
    session->FaceFeatureExtract(image_process, faces[0], feature);

    // ============ 4. ★ 去重查找：先在库中搜索是否已有此人 ★ ============
    inspire::FaceSearchResult search_result;
    INSPIREFACE_FEATURE_HUB->SearchFaceFeature(
        feature.embedding,                                   // 提取到的特征
        search_result,                                       // 搜索结果
        false                                                // ★ auto_insert=false：不自动录入，只搜索
    );

    if (search_result.id != INSPIRE_INVALID_ID               // 找到匹配
        && search_result.similarity > FACE_RECOGNITION_THRESHOLD) {
        // -------- 已存在 → 返回已有的 feature_id --------
        feature_id = (int)search_result.id;
        is_duplicate = true;                                 // 标记为重复
        free(bgr_buffer);
        return 0;
    }

    // ============ 5. 新人 → 插入特征库 ============
    int64_t out_id;
    auto ret = INSPIREFACE_FEATURE_HUB->FaceFeatureInsert(
        feature.embedding,                                   // 特征向量
        INSPIRE_INVALID_ID,                                  // 让 SDK 自动分配 ID
        out_id                                               // 输出：SDK 分配的新 ID
    );
    free(bgr_buffer);

    if (ret == HSUCCEED) {
        feature_id = (int)out_id;                            // 传出新 ID
        is_duplicate = false;                                // 不是重复
        return 0;
    }
    return -1;
}

void FaceModule::deinit() {
    if (session_) {
        delete (std::shared_ptr<inspire::Session>*)session_;
        session_ = nullptr;
    }
    inited_ = false;
    std::cout << "[FaceModule] Deinit done" << std::endl;
}