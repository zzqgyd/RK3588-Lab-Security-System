#ifndef INFER_THREAD_HPP
#define INFER_THREAD_HPP

#include "queue_manager.hpp"
#include "yolov5_infer.h"
#include "image_drawing.h"
#include <thread>
#include <atomic>

/**
 * @brief 推理线程类
 * 
 * 职责：
 * 1. 从队列管理器拉取帧数据（消费者）
 * 2. 调用 YOLO 模型进行推理
 * 3. 在图像上绘制检测结果
 * 4. 管理帧内存的释放
 * 
 * 线程模型：
 * - 单工作线程轮询所有流队列
 * - 非阻塞，队列空时短暂休眠
 * 
 * 所有权说明：
 * - 不持有 QueueManager（外部传入，外部释放）
 * - 不持有 yolo_ctx_ 数组（外部传入，外部释放）
 * - 持有工作线程 th_（自己管理生命周期）
 */
class InferThread
{
public:
    /**
     * @brief 构造函数
     * @param qm           队列管理器指针（不持有所有权）
     * @param stream_num   实际处理的流路数（≤ MAX_STREAM）
     * @param yolo_ctx_array YOLO上下文数组指针（每路一个，不持有所有权）
     */
    InferThread(QueueManager* qm, int stream_num, rknn_app_context_t* yolo_ctx_array);
    
    /**
     * @brief 析构函数
     * 自动调用 stop() 等待工作线程退出
     */
    ~InferThread();

    /**
     * @brief 启动推理线程
     * 创建新线程并开始执行 loop()
     * 可重复调用（内部有防重复启动保护）
     */
    void start();

    /**
     * @brief 停止推理线程
     * 设置退出标志，阻塞等待工作线程结束
     * 线程退出前会清空队列中剩余帧
     */
    void stop();

private:
    /**
     * @brief 工作线程主循环
     * 轮询所有流队列，有帧则处理，无帧则短暂休眠
     */
    void loop();
    
    /**
     * @brief 处理单帧数据
     * @param frame 帧指针（已获得所有权，调用后负责释放）
     * 
     * 处理流程：
     * 1. 校验帧数据有效性
     * 2. 获取对应流的 YOLO 上下文
     * 3. 执行推理
     * 4. 绘制检测框
     * 5. 释放帧内存
     */
    void process(Frame* frame);
    
    /**
     * @brief 清空队列中的剩余帧
     * 线程退出时调用，避免内存泄漏
     * 直接释放帧，不再进行推理
     */
    void drain_queues();

private:
    QueueManager* qm_;                ///< 队列管理器（外部传入，不持有）
    int stream_num_;                  ///< 实际流路数
    rknn_app_context_t* yolo_ctx_;    ///< YOLO上下文数组（外部传入，不持有）
    std::atomic<bool> running_;       ///< 运行标志（线程安全）
    std::thread th_;                  ///< 工作线程对象
};

#endif