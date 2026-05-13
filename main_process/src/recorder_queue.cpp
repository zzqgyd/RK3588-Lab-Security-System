/**
 * @file recorder_queue.cpp
 * @brief 独立录像线程池实现 - 每路流一个录像线程
 * 
 * ================================================================
 * 核心改进
 * ================================================================
 * 
 * 1. Per-stream 队列：每路流一个 MPMC 队列，避免任务分发竞争
 * 2. Per-stream 线程：每路流一个录像线程，独占处理
 * 3. FramePtr 引用计数：RecorderTask 持有 FramePtr，编码期间 buffer 有效
 * 4. 原子状态管理：is_recording 使用 atomic，Worker 线程可安全查询
 */

#include "recorder_queue.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>
#include <sys/statvfs.h>
#include <dirent.h>
#include <algorithm>
#include <vector>
#include <sys/stat.h>
#include "ipc/db.h"

// 录像文件输出目录
#define VIDEO_OUTPUT_DIR "/mnt/sdcard/videos"
#define MIN_FREE_SPACE_MB    500    // SD 卡最少保留 500MB
#define MAX_VIDEO_AGE_DAYS   30     // 录像最多保留 30 天

// 外部全局配置（main.cpp 中定义）
extern AppConfig g_cfg;
extern void* g_main_db;  // 在 worker_pool.cpp 中定义

static long get_free_space_mb(const char* path) {
    struct statvfs stat;
    if (statvfs(path, &stat) != 0) return -1;
    return (long)(stat.f_bavail * stat.f_frsize) / (1024 * 1024);
}

static void cleanup_old_videos(const char* dir, long min_free_mb, int max_days) {
    time_t now = time(NULL);
    time_t cutoff = now - max_days * 86400;

    DIR* d = opendir(dir);
    if (!d) return;

    struct dirent* entry;
    std::vector<std::pair<time_t, std::string>> files;

    while ((entry = readdir(d)) != NULL) {
        if (strstr(entry->d_name, ".mp4") == NULL) continue;

        std::string fullpath = std::string(dir) + "/" + entry->d_name;
        struct stat st;
        if (stat(fullpath.c_str(), &st) == 0) {
            if (st.st_mtime < cutoff) {
                printf("[RecorderPool] Expired: %s\n", fullpath.c_str());
                remove(fullpath.c_str());
                // 新增：删除数据库中的记录
                if (g_main_db) {
                    db_delete_video_by_path(g_main_db, fullpath.c_str());
                }
                continue;
            }
            files.push_back({st.st_mtime, fullpath});
        }
    }
    closedir(d);

    std::sort(files.begin(), files.end());
    for (auto& f : files) {
        long free_mb = get_free_space_mb(dir);
        if (free_mb >= min_free_mb) break;
        printf("[RecorderPool] Deleting: %s\n", f.second.c_str());
        remove(f.second.c_str());
        // 新增：删除数据库中的记录
        if (g_main_db) {
            db_delete_video_by_path(g_main_db, f.second.c_str());
        }
    }
}

/* ================================================================
 * RecorderPool 构造/析构
 * ================================================================ */
RecorderPool::RecorderPool(int channel_count)
    : channel_count_(channel_count)
    , running_(false)
{
    // ---- 创建 per-stream 任务队列 ----
    // 每路流一个独立队列，容量 256 防止推理线程阻塞
    stream_queues_.reserve(channel_count);
    for (int i = 0; i < channel_count; i++) {
        stream_queues_.push_back(std::make_unique<MPMCQueue<RecorderTask>>(256));
    }

    // ---- 创建 per-stream 录像器上下文 ----
    contexts_.resize(channel_count);

    printf("[RecorderPool] Created: %d channels, per-stream architecture\n", channel_count);
}

RecorderPool::~RecorderPool()
{
    stop();
}

/* ================================================================
 * start: 启动所有录像线程
 * ================================================================ */
void RecorderPool::start()
{
    if (running_) return;
    running_ = true;

    // ---- 1. 创建输出目录 ----
    mkdir(VIDEO_OUTPUT_DIR, 0777);
    cleanup_old_videos(VIDEO_OUTPUT_DIR, MIN_FREE_SPACE_MB, MAX_VIDEO_AGE_DAYS);  

    // ---- 2. 为每路流创建 VideoRecorder 实例 ----
    for (int i = 0; i < channel_count_; i++) {
        // 从配置中获取分辨率（如果有的话）
        // 这里使用默认值，实际会从帧中动态获取
        int width = 1920;   // 默认宽度
        int height = 1080;  // 默认高度

        // 尝试从 channel 配置获取实际分辨率
        if (i < g_cfg.channel_count) {
            // 如果有配置的分辨率，这里可以读取
            // width = g_cfg.channels[i].width;
            // height = g_cfg.channels[i].height;
        }

        contexts_[i].recorder = vr_create(
            VIDEO_OUTPUT_DIR,
            i,
            width,
            height,
            4  // fmt 保留参数，实际固定 H.264
        );

        if (!contexts_[i].recorder) {
            fprintf(stderr, "[RecorderPool] Warning: Failed to create recorder for stream %d\n", i);
        } else {
            printf("[RecorderPool] Stream %d: Recorder created (%dx%d)\n", i, width, height);
        }

        contexts_[i].is_recording.store(false);
        contexts_[i].current_device_id = -1;
    }

    // ---- 3. 启动 per-stream 录像线程 ----
    // 每路流一个线程，独占处理该流的所有任务
    for (int i = 0; i < channel_count_; i++) {
        threads_.emplace_back(&RecorderPool::recorder_loop, this, i);
    }

    printf("[RecorderPool] Started %d recorder threads (one per stream)\n", channel_count_);
}

/* ================================================================
 * stop: 停止所有录像线程
 * ================================================================ */
void RecorderPool::stop()
{
    if (!running_) return;
    running_ = false;

    // ---- 1. 等待所有队列清空（最多 3 秒）----
    int wait_count = 0;
    bool all_empty = false;
    while (!all_empty && wait_count < 300) {
        all_empty = true;
        for (int i = 0; i < channel_count_; i++) {
            if (stream_queues_[i] && stream_queues_[i]->size() > 0) {
                all_empty = false;
                break;
            }
        }
        if (!all_empty) {
            usleep(10000);  // 10ms
            wait_count++;
        }
    }

    if (!all_empty) {
        int total_pending = 0;
        for (int i = 0; i < channel_count_; i++) {
            if (stream_queues_[i]) {
                total_pending += stream_queues_[i]->size();
            }
        }
        printf("[RecorderPool] Warning: %d tasks still pending after timeout\n", total_pending);
    }

    // ---- 2. 等待所有录像线程退出 ----
    for (auto& th : threads_) {
        if (th.joinable()) th.join();
    }
    threads_.clear();

    // ---- 3. 销毁所有录像器 ----
    for (int i = 0; i < channel_count_; i++) {
        if (contexts_[i].recorder) {
            // 如果还在录像，先停止
            if (contexts_[i].is_recording.load()) {
                printf("[RecorderPool] Stream %d: Auto-stopping on exit\n", i);
                char* path = vr_stop(contexts_[i].recorder, "exit");
                if (path) {
                    printf("[RecorderPool] Stream %d: Saved → %s\n", i, path);
                    free(path);
                }
            }
            vr_destroy(contexts_[i].recorder);
            contexts_[i].recorder = nullptr;
        }
    }

    printf("[RecorderPool] Stopped\n");
}

/* ================================================================
 * submit: 提交任务到指定 stream 的队列（非阻塞）
 * 
 * 多 Worker 线程可并发调用，MPMC 队列保证线程安全
 * ================================================================ */
bool RecorderPool::submit(RecorderTask&& task)
{
    if (!running_) return false;

    int stream_id = task.stream_id;
    if (stream_id < 0 || stream_id >= channel_count_) {
        fprintf(stderr, "[RecorderPool] Invalid stream_id: %d\n", stream_id);
        return false;
    }

    if (!stream_queues_[stream_id]) {
        fprintf(stderr, "[RecorderPool] Stream %d: Queue not initialized\n", stream_id);
        return false;
    }

    // 非阻塞入队，队列满时返回 false（推理线程不会被阻塞）
    return stream_queues_[stream_id]->try_enqueue(std::move(task));
}

/* ================================================================
 * recorder_loop: Per-stream 录像线程主循环
 * 
 * 每个线程只处理自己负责的那一路流，无需任务分配逻辑
 * ================================================================ */
void RecorderPool::recorder_loop(int stream_id)
{
    printf("[Recorder %d] Started (handles stream %d only)\n", stream_id, stream_id);

    while (running_) {
        RecorderTask task;

        // 非阻塞取任务（只从自己负责的队列取）
        if (!stream_queues_[stream_id]->try_dequeue(task)) {
            // 队列空，短暂休眠避免忙等
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // 处理任务
        process_task(contexts_[stream_id], std::move(task), stream_id);
    }

    // ---- 退出前清空残留任务 ----
    RecorderTask leftover;
    while (stream_queues_[stream_id]->try_dequeue(leftover)) {
        process_task(contexts_[stream_id], std::move(leftover), stream_id);
    }

    printf("[Recorder %d] Stopped\n", stream_id);
}

/* ================================================================
 * process_task: 处理单个录像任务
 * 
 * 注意：FRAME 任务中 task.frame 持有 FramePtr，处理完成后自动析构
 * ================================================================ */
void RecorderPool::process_task(RecorderContext& ctx, RecorderTask&& task, int stream_id)
{
    switch (task.type) {
    case RecorderTaskType::FRAME: {
        // ============================================================
        // 喂帧任务
        //
        // 生命周期说明：
        //   task.frame 是 FramePtr（shared_ptr<Frame>）
        //   此函数持有引用 → gst_sample 不会被释放 → dmabuf fd 有效
        //   vr_feed_frame 内部创建 AVBufferRef → 编码器持有引用
        //   函数返回后 task 析构 → FramePtr 引用计数 -1
        //   编码器完成编码后释放 AVBufferRef → 不再引用 fd
        //   此时如果 FramePtr 是最后一个引用 → GstBuffer 被释放
        // ============================================================
        if (ctx.is_recording.load(std::memory_order_acquire) && ctx.recorder) {
            if (task.frame) {
                int ret = vr_feed_frame(
                    ctx.recorder,
                    task.frame->img.virt_addr,
                    task.frame->img.fd,
                    task.frame->img.size,
                    task.pts
                );
                if (ret < 0) {
                    fprintf(stderr, "[Recorder %d] Stream %d: Frame encoding failed\n",
                            stream_id, stream_id);
                    // 编码失败不停止录像，继续处理后续帧
                }
            }
        }
        // task.frame 在这里自动析构，FramePtr 引用计数 -1
        // break 跳出 switch，task 对象离开作用域 → 析构。
        break;
    }

    case RecorderTaskType::START: {
        // ============================================================
        // 开始录像
        //
        // 使用 CAS（Compare-And-Swap）确保原子性：
        //   只有当前状态是 false 时才设置为 true
        //   防止多个 START 任务重复开始录像
        // ============================================================
        cleanup_old_videos(VIDEO_OUTPUT_DIR, MIN_FREE_SPACE_MB, MAX_VIDEO_AGE_DAYS);  
        bool expected = false;
        if (ctx.is_recording.compare_exchange_strong(expected, true)) {
            ctx.current_device_id = task.device_id;
            ctx.start_time = std::chrono::steady_clock::now();
            strncpy(ctx.start_time_str, task.start_time, 63);  // 新增

            if (ctx.recorder) {
                int ret = vr_start(ctx.recorder, task.start_time);
                if (ret == 0) {
                    printf("[Recorder %d] Stream %d: START recording (device %d, time=%s)\n",
                           stream_id, stream_id, task.device_id, task.start_time);
                } else {
                    fprintf(stderr, "[Recorder %d] Stream %d: START failed\n",
                            stream_id, stream_id);
                    // 失败时回退状态
                    ctx.is_recording.store(false);
                }
            } else {
                fprintf(stderr, "[Recorder %d] Stream %d: No recorder instance\n",
                        stream_id, stream_id);
                ctx.is_recording.store(false);
            }
        } else {
            printf("[Recorder %d] Stream %d: Already recording, ignore duplicate START\n",
                   stream_id, stream_id);
        }
        break;
    }

    case RecorderTaskType::STOP: {
        // ============================================================
        // 停止录像
        //
        // 使用 CAS 确保只停止一次：
        //   只有当前状态是 true 时才设置为 false
        //   防止多个 STOP 任务重复停止
        // ============================================================
        bool expected = true;
        if (ctx.is_recording.compare_exchange_strong(expected, false)) {
            if (ctx.recorder) {
                char* path = vr_stop(ctx.recorder, task.end_time);
                if (path) {
                    printf("[Recorder %d] Stream %d: STOP recording (device %d, time=%s, file=%s)\n",
                           stream_id, stream_id, task.device_id,
                           task.end_time, path);
                    // 新增
                    if (g_main_db) {
                        const char* type = ctx.is_registered ? "registered" : "unregistered";
                        db_insert_video_record(g_main_db, stream_id,
                                            ctx.start_time_str, task.end_time, path, type);
                    }
                    free(path);
                } else {
                    fprintf(stderr, "[Recorder %d] Stream %d: STOP failed (no path returned)\n",
                            stream_id, stream_id);
                }
            }
            ctx.current_device_id = -1;
            ctx.is_registered = false;
        } else {
            printf("[Recorder %d] Stream %d: Not recording, ignore duplicate STOP\n",
                   stream_id, stream_id);
        }
        break;
    }

    default:
        break;
    }
}