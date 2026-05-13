/**
 * @file detection_state.cpp
 * @brief 检测状态机实现 - Stream级录像管理
 */

#include "detection_state.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 获取当前单调时钟时间
 */
static struct timespec now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}

/**
 * @brief 计算两个时间戳的毫秒差
 */
static long elapsed_ms(struct timespec start, struct timespec end)
{
    return (end.tv_sec - start.tv_sec) * 1000 +
           (end.tv_nsec - start.tv_nsec) / 1000000;
}

/**
 * @brief 初始化状态管理器
 */
void ds_init(DetectionStateManager* mgr, int stay_ms, int absence_ms)
{
    if (!mgr) return;
    
    memset(mgr, 0, sizeof(*mgr));
    mgr->stay_threshold_ms = stay_ms;
    mgr->absence_threshold_ms = absence_ms;
    
    // 初始化每路流的Stream状态
    for (int i = 0; i < MAX_CHANNEL; i++) {
        mgr->streams[i].stream_id = i;
        mgr->streams[i].stream_recording = false;
        mgr->streams[i].recording_device_id = -1;
        mgr->streams[i].last_active_device = -1;
    }
    
    printf("[State] Init: stay=%dms, absence=%dms\n", stay_ms, absence_ms);
}

/**
 * @brief 更新检测结果
 */
int ds_update(DetectionStateManager* mgr,
              const int boxes[][4], int box_count,
              int stream_id, const DeviceROI* roi)
{
    // 参数校验
    if (!mgr || !roi) return 0;
    if (stream_id < 0 || stream_id >= MAX_CHANNEL) return 0;
    
    StreamState* ss = &mgr->streams[stream_id];
    
    // ============================================================
    // 第一步：查找或创建设备状态
    // ============================================================
    DeviceState* ds = NULL;
    for (int i = 0; i < ss->device_count; i++) {
        if (ss->devices[i].device_id == roi->device_id) {
            ds = &ss->devices[i];
            break;
        }
    }
    
    // 没找到，创建新设备状态
    if (!ds) {
        for (int i = 0; i < ss->device_count; i++) {
            if (ss->devices[i].device_id == 0) {  // device_id=0 表示空闲槽位
                ds = &ss->devices[i];
                ds->device_id = roi->device_id;
                ds->person_present = false;
                ds->triggered = false;
                ds->silent_until_expire = false;
                break;
            }
        }
    }
    
    // 设备数量超限，忽略
    if (!ds) return 0;
    
    // ============================================================
    // 第二步：判断检测框是否在ROI内
    // ============================================================
    bool found = false;
    for (int i = 0; i < box_count; i++) {
        if (roi_is_inside(roi, boxes[i][0], boxes[i][1], 
                          boxes[i][2], boxes[i][3])) {
            found = true;
            break;
        }
    }
    
    struct timespec t = now();
    int result = 0;
    
    // ============================================================
    // 第三步：更新设备级状态（人在/离开）
    // ============================================================
    if (found && !ds->person_present) {
        // 刚检测到人
        ds->person_present = true;
        ds->first_seen = t;
        ds->last_seen = t;
        printf("[State] Stream %d, Device %d: person entered\n", 
               stream_id, roi->device_id);
    } 
    else if (found && ds->person_present) {
        // 持续有人：只更新最后出现时间
        ds->last_seen = t;
    } 
    else if (!found && ds->person_present) {
        // 人离开：重置 triggered，允许下一次进入重新触发人脸识别
        ds->person_present = false;
        ds->last_seen = t;
        ds->triggered = false;
        printf("[State] Stream %d, Device %d: person left\n",
               stream_id, roi->device_id);
    }
    
    // ============================================================
    // 第四步：触发登记（人脸识别）
    // ============================================================
    // 条件：有人 + 未触发过 + 不在免打扰 + 停留超阈值
    if (ds->person_present && !ds->triggered && !ds->silent_until_expire) {
        long ms = elapsed_ms(ds->first_seen, t);
        if (ms >= mgr->stay_threshold_ms) {
            ds->triggered = true;
            result |= 1;  // bit0: 触发人脸识别
            printf("[State] Stream %d, Device %d: triggered recognition (stay=%ldms)\n",
                   stream_id, roi->device_id, ms);
        }
    }
    
    // ============================================================
    // 第五步：Stream级录像决策
    // ============================================================
    
    // 5.1 检查当前是否有设备正在录像条件（停留超阈值）
    //     注意：免打扰期间（已人脸识别成功=正在使用）也应当录像，
    //           所以这里不再排除 silent_until_expire 的设备。
    bool any_device_active = false;
    int active_device = -1;
    long max_stay_ms = 0;
    
    for (int i = 0; i < ss->device_count; i++) {
        if (ss->devices[i].device_id == 0) continue;  // 空槽位
        
        if (ss->devices[i].person_present) {
            long ms = elapsed_ms(ss->devices[i].first_seen, t);
            if (ms >= mgr->stay_threshold_ms) {
                any_device_active = true;
                active_device = ss->devices[i].device_id;
                if (ms > max_stay_ms) max_stay_ms = ms;
                break;  // 只要有一个设备满足就行
            }
        }
    }
    
    // 5.2 更新最近活跃设备（用于防抖）
    if (active_device >= 0) {
        ss->last_active_device = active_device;
        ss->last_active_time = t;
    }
    
    // 5.3 决策：开始录像
    // 条件：当前未录像 + 有设备活跃
    if (!ss->stream_recording && any_device_active) {
        result |= 2;  // bit1: 需要开始录像
        ss->stream_recording = true;
        ss->recording_device_id = active_device;
        ss->recording_start_time = t;
        printf("[State] Stream %d: START recording (device %d, stay=%ldms)\n",
               stream_id, active_device, max_stay_ms);
    }
    
    // 5.4 决策：停止录像
    // 条件：正在录像 + 无设备活跃 + 超过离开阈值
    if (ss->stream_recording && !any_device_active) {
        long ms = elapsed_ms(ss->recording_start_time, t);
        if (ms >= mgr->absence_threshold_ms) {
            result |= 4;  // bit2: 需要停止录像
            ss->stream_recording = false;
            ss->recording_device_id = -1;
            printf("[State] Stream %d: STOP recording (inactive=%ldms)\n",
                   stream_id, ms);
        }
    }
    
    // 5.5 注意：多个设备同时满足条件时，不会重复触发开始录像
    // 因为 ss->stream_recording 已经为 true，不会再进入开始分支
    
    // ============================================================
    // 第六步：免打扰到期处理
    // ============================================================
    // 旧逻辑 elapsed_ms(t, expire) 是 expire-now，未到期>0 反而误清；
    // 正确：用 now-expire >=0 表示已到期，清除免打扰并重置 triggered，
    //       让设备恢复"空闲"，下一个人进入可重新触发识别。
    if (ds->silent_until_expire) {
        long passed = elapsed_ms(ds->silent_expire_time, t);  // now - expire
        if (passed >= 0) {
            ds->silent_until_expire = false;
            ds->triggered = false;   // 到期后允许重新触发
            printf("[State] Stream %d, Device %d: silent period expired\n",
                   stream_id, roi->device_id);
        }
    }
    
    return result;
}

/**
 * @brief 设置免打扰模式
 */
void ds_set_silent(DetectionStateManager* mgr,
                   int stream_id, int device_id, int duration_minutes)
{
    if (!mgr) return;
    if (stream_id < 0 || stream_id >= MAX_CHANNEL) return;
    
    StreamState* ss = &mgr->streams[stream_id];
    
    for (int i = 0; i < ss->device_count; i++) {
        if (ss->devices[i].device_id == device_id) {
            ss->devices[i].silent_until_expire = true;
            struct timespec t = now();
            ss->devices[i].silent_expire_time.tv_sec = t.tv_sec + duration_minutes * 60;
            ss->devices[i].silent_expire_time.tv_nsec = t.tv_nsec;
            printf("[State] Stream %d, Device %d: silent for %d minutes\n",
                   stream_id, device_id, duration_minutes);
            return;
        }
    }
}

/**
 * @brief 查询某路流是否正在录像
 */
bool ds_is_stream_recording(DetectionStateManager* mgr, int stream_id)
{
    if (!mgr) return false;
    if (stream_id < 0 || stream_id >= MAX_CHANNEL) return false;
    return mgr->streams[stream_id].stream_recording;
}

/**
 * @brief 重置设备状态
 */
void ds_reset_device(DetectionStateManager* mgr, int stream_id, int device_id)
{
    if (!mgr) return;
    if (stream_id < 0 || stream_id >= MAX_CHANNEL) return;

    StreamState* ss = &mgr->streams[stream_id];
    for (int i = 0; i < ss->device_count; i++) {
        if (ss->devices[i].device_id == device_id) {
            memset(&ss->devices[i], 0, sizeof(DeviceState));
            ss->devices[i].device_id = device_id;
            printf("[State] Stream %d, Device %d: reset\n", stream_id, device_id);
            return;
        }
    }
}

/**
 * @brief 应用新的 ROI 配置（热重载时调用）
 *
 * 同步流数量/阈值，重置每路设备槽位的 device_id 与检测状态，
 * 保留 stream_recording 等流级录像状态以与 RecorderPool 保持一致。
 * 调用者需在外层加锁。
 */
void ds_apply_roi(DetectionStateManager* mgr, const ROIConfig* roi)
{
    if (!mgr || !roi) return;

    mgr->stream_count        = roi->stream_count;
    mgr->stay_threshold_ms   = roi->person_stay_threshold;
    mgr->absence_threshold_ms = roi->absence_threshold;

    for (int i = 0; i < roi->stream_count && i < MAX_CHANNEL; i++) {
        StreamState* ss = &mgr->streams[i];
        ss->stream_id = i;

        int new_count = roi->streams[i].device_count;
        if (new_count < 0) new_count = 0;
        if (new_count > MAX_DEVICES_EACH) new_count = MAX_DEVICES_EACH;

        // 清空所有设备槽位状态
        for (int d = 0; d < MAX_DEVICES_EACH; d++) {
            memset(&ss->devices[d], 0, sizeof(DeviceState));
        }
        ss->device_count = new_count;
        for (int d = 0; d < new_count; d++) {
            ss->devices[d].device_id = roi->streams[i].devices[d].device_id;
        }
        // 保留：stream_recording / recording_device_id / recording_start_time
        //        last_active_device / last_active_time
        printf("[State] Stream %d: apply roi (devices=%d)\n", i, new_count);
    }
}