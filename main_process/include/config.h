#ifndef CONFIG_H
#define CONFIG_H

// ==================== 编译期容量 ====================
#define MAX_CHANNEL         8     // 最大流路数（数组容量）
#define MAX_DEVICES_EACH    4     // 每路最大设备ROI数
#define MAX_DETECTIONS      64    // 每帧最大检测框数
#define CONFIG_LINE_MAX     256   // 配置文件内容行数

// ==================== 运行时可调参数 ====================
#define WORKER_NUM          6     // Worker线程数
#define FRAMES_PER_STREAM   6     // 帧池容量（每路）
#define RING_SIZE           32    // RingBuffer容量（2的幂）
#define MPMC_CAPACITY       256   // MPMC无锁队列容量
#define PIPELINE_QUEUE_SIZE 32     // 流水线内部队列容量

#include "common.h"
#include <gst/gst.h>
#include <stdint.h>
#include <memory>
#include <sys/mman.h>
#include <mutex>
#include "image_utils.h"  
#include "postprocess.h"  

// ===== 前向声明 =====
struct GstDecoder;
class FramePool;

/* ================================================================
 * 检测框结构体（原始坐标系，未缩放）
 * ================================================================ */
#ifndef IPC_DETECTION_BOX_DEFINED
#define IPC_DETECTION_BOX_DEFINED
struct DetectionBox {
    int x, y, w, h;
};
#endif


/**
 * @brief 统一帧结构（贯穿整个系统）
 * 
 * Step 3: 添加 DMA-BUF 支持
 */
struct Frame
{
    image_buffer_t img;      // 图像数据（包含 fd 和 virt_addr）
    uint64_t pts;            // 时间戳
    uint64_t seq;            // 帧序号
    int stream_id;           // 流ID
    
    // ===== Step 3 新增：DMA-BUF 支持 =====
    GstSample* gst_sample;   // DMA-BUF 模式下持有 GStreamer sample 引用
    bool is_dmabuf;          // 是否为 DMA-BUF 模式
    
    Frame()
    {
        memset(&img, 0, sizeof(img));
        img.fd = -1;
        pts = 0;
        seq = 0;
        stream_id = 0;
        gst_sample = nullptr;
        is_dmabuf = false;
    }
    
    ~Frame()
    {
        // ===== Step 3 改动：区分释放方式 =====
        if (is_dmabuf) {
            // DMA-BUF 模式：只需要释放 sample 引用，fd 由 GStreamer 管理
            if (img.virt_addr) {
                munmap(img.virt_addr, img.size);
                img.virt_addr = nullptr;
            }
            if (gst_sample) {
                gst_sample_unref(gst_sample);
                gst_sample = nullptr;
            }
            // 注意：img.fd 是借来的，不需要 close
        } else {
            // 回退模式：释放 malloc 内存
            if (img.virt_addr) {
                free(img.virt_addr);
                img.virt_addr = nullptr;
            }
        }
        img.fd = -1;
    }
    
    // 禁止拷贝
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
};

// 类型别名：简化 shared_ptr<Frame> 的使用
using FramePtr = std::shared_ptr<Frame>;

// ===== 通道上下文（每路一个）=====
class ChannelContext {
public:
    int id = 0;                     //这一路流id
    GstDecoder* decoder = nullptr;
    char rtsp[256] = {0};           //具体流地址
    FramePool* frame_pool = nullptr;  // 每路独立的帧池
    
    // 显示缓存：shared_ptr，不再 memcpy 整帧
    FramePtr latest_frame;
    mutable std::mutex frame_lock;

    // 检测框坐标（Worker 线程写入，主线程读取）
    DetectionBox boxes[MAX_DETECTIONS];
    int box_count;
    mutable std::mutex box_lock;
    
    ChannelContext() = default;
    
    // 禁止拷贝
    ChannelContext(const ChannelContext&) = delete;
    ChannelContext& operator=(const ChannelContext&) = delete;
    
    FramePtr get_frame() const {
        std::lock_guard<std::mutex> lock(frame_lock);
        return latest_frame;
    }
    
    void set_frame(FramePtr frame) {
        std::lock_guard<std::mutex> lock(frame_lock);
        latest_frame = std::move(frame);
    }

    void set_boxes(const DetectionBox* b, int cnt) {
        std::lock_guard<std::mutex> lk(box_lock);
        box_count = (cnt > MAX_DETECTIONS) ? MAX_DETECTIONS : cnt;
        memcpy(boxes, b, box_count * sizeof(DetectionBox));
    }
    void get_boxes(DetectionBox* out, int& cnt) const {
        std::lock_guard<std::mutex> lk(box_lock);
        cnt = box_count;
        memcpy(out, boxes, cnt * sizeof(DetectionBox));
    }
};

// ===== 应用配置 =====
typedef struct {
    char                 model_path[256];
    ChannelContext       channels[MAX_CHANNEL];
    int                  channel_count;
} AppConfig;

// 全局配置
extern AppConfig g_cfg;

#endif