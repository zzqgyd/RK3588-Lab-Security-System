#ifndef ROI_CONFIG_H
#define ROI_CONFIG_H

#include <stdint.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 一个设备区域的定义
 * 坐标相对于摄像头画面（和 YOLO 检测框同一坐标系）
 */
typedef struct {
    int x, y;           // 左上角
    int w, h;           // 宽高
    int device_id;      // 设备编号（房间内唯一）真实不为0
} DeviceROI;

/*
 * 一路流的 ROI 配置
 */
typedef struct {
    int stream_id;          // 流 ID（对应第几路）
    int device_count;       // 该路有几个设备需要检测
    DeviceROI devices[MAX_DEVICES_EACH];   // 最多 MAX_DEVICES_EACH 个设备区域
} StreamROIConfig;

/*
 * 全局 ROI 配置
 */
typedef struct {
    int stream_count;            //几路流
    StreamROIConfig streams[MAX_CHANNEL];  // 最多 MAX_CHANNEL 路
    int person_stay_threshold;   // 人停留超过多少毫秒触发（默认 5000ms = 5秒）
    int absence_threshold;       // 人消失超过多少毫秒停止录像（默认 5000ms = 5秒）
} ROIConfig;

/*
 * 加载 ROI 配置文件
 * 文件格式（每行）：
 *   stream_id device_id x y w h
 * 示例：
 *   0 1 100 200 300 400
 *   0 2 500 200 300 400
 *   1 1 150 250 350 450
 */
int roi_config_load(const char* path, ROIConfig* cfg);

/*
 * 判断一个检测框是否在某个设备区域内
 * @return 1=在区域内, 0=不在
 */
int roi_is_inside(const DeviceROI* roi, int box_x, int box_y, int box_w, int box_h);

#ifdef __cplusplus
}
#endif

#endif // ROI_CONFIG_H