/**
 * @file detection_state.h
 * @brief 检测状态机 - Stream级录像管理
 * 
 * 核心设计：
 * 1. 每路流独立管理多个设备ROI
 * 2. Stream级录像：只要有一个设备满足条件就录像
 * 3. 多个设备同时满足不重复触发
 * 4. 所有设备都不满足 + 超阈值才停止
 */

#ifndef DETECTION_STATE_H
#define DETECTION_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "roi_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 每个设备的检测状态
 */
typedef struct {
    int device_id;                      // 设备编号
    bool person_present;                // 当前是否有人在区域内
    struct timespec first_seen;         // 第一次检测到人的时间
    struct timespec last_seen;          // 最后一次检测到人的时间
    bool triggered;                     // 是否已触发登记（人脸识别）
    bool silent_until_expire;           // 免打扰模式
    struct timespec silent_expire_time; // 免打扰到期时间
} DeviceState;

/**
 * @brief 每路流的状态（Stream级聚合）
 */
typedef struct {
    int stream_id;                      // 流ID
    int device_count;                   // 该路有几个设备
    DeviceState devices[MAX_DEVICES_EACH];  // 设备状态数组
    
    // ===== Stream级录像状态 =====
    bool stream_recording;              // 该路流是否正在录像
    int recording_device_id;            // 哪个设备触发的录像（-1表示无）
    struct timespec recording_start_time;   // 录像开始时间
    
    // ===== 防抖：避免频繁切换 =====
    int last_active_device;             // 上一次活跃的设备
    struct timespec last_active_time;   // 上一次活跃的时间
} StreamState;

/**
 * @brief 全局状态管理器
 */
typedef struct {
    int stream_count;                   // 流数量
    StreamState streams[MAX_CHANNEL];   // 每路流状态
    int stay_threshold_ms;              // 停留阈值（毫秒）
    int absence_threshold_ms;           // 离开阈值（毫秒）
} DetectionStateManager;

/**
 * @brief 初始化状态管理器
 * @param mgr       状态管理器
 * @param stay_ms   停留多少毫秒触发
 * @param absence_ms 离开多少毫秒停止
 */
void ds_init(DetectionStateManager* mgr, int stay_ms, int absence_ms);

/**
 * @brief 更新检测结果（核心函数）
 * 
 * 流程：
 *   1. 更新设备级状态（人在/离开、停留计时）
 *   2. 判断是否需要触发人脸识别
 *   3. 判断Stream级录像状态
 *   4. 返回事件位掩码
 * 
 * @param mgr        状态管理器
 * @param boxes      所有检测框 [x,y,w,h]
 * @param box_count  检测框数量
 * @param stream_id  流ID
 * @param roi        设备ROI配置
 * @return 事件位掩码：
 *   bit0 (1) = 触发登记（需要人脸识别）
 *   bit1 (2) = 该路流需要开始录像
 *   bit2 (4) = 该路流需要停止录像
 *   bit3 (8) = 录像设备切换（需先停后启，暂未使用）
 */
int ds_update(DetectionStateManager* mgr,
              const int boxes[][4], int box_count,
              int stream_id, const DeviceROI* roi);

/**
 * @brief 设置免打扰模式
 * @param mgr       状态管理器
 * @param stream_id 流ID
 * @param device_id 设备ID
 * @param duration_minutes 免打扰时长（分钟）
 */
void ds_set_silent(DetectionStateManager* mgr,
                   int stream_id, int device_id, int duration_minutes);

/**
 * @brief 查询某路流是否正在录像
 */
bool ds_is_stream_recording(DetectionStateManager* mgr, int stream_id);

/**
 * @brief 重置设备状态（用于调试）
 */
void ds_reset_device(DetectionStateManager* mgr, int stream_id, int device_id);

/**
 * @brief 应用新的 ROI 配置到状态机（用于 ROI 热重载）
 *
 * 行为：
 *   - 同步 stream_count / 阈值
 *   - 每路流按新配置重置 device_count 与各槽位 device_id
 *   - 清空设备级检测状态（人在/触发/免打扰），避免与旧 ROI 错位
 *   - 保留 stream_recording 等流级录像状态，避免与 RecorderPool 失同步
 *
 * 注意：调用者需自行保证线程安全（在外层加锁后调用）。
 */
void ds_apply_roi(DetectionStateManager* mgr, const ROIConfig* roi);

#ifdef __cplusplus
}
#endif

#endif // DETECTION_STATE_H