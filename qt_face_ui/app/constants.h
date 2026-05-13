#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <QString>
#include <cstdint>
#include <stdint.h>

// ================================================================
// IPC 公共定义（Socket 路径、FrameMeta、FaceCommand、FaceEvent、DetectionBox）
// 统一从 ipc/ipc_socket.h 引入，避免与主进程/人脸进程定义漂移
// ================================================================
#include "ipc/ipc_socket.h"

// ================================================================
// 数据库 / 配置文件路径（Qt 进程私有，相对工作目录）
// ================================================================
#define DB_PATH             "../../config/records.db"
#define ROI_CONFIG_PATH     "../../config/roi.conf"

// ================================================================
// 显示参数（Qt 进程私有）
// ================================================================
#define MAX_STREAMS         8
#define GRID_COLS           4
#define GRID_ROWS           2
#define ROI_MAX_DEVICES     4     // 每路流最大设备 ROI 数（与主进程 MAX_DEVICES_EACH 对齐）

// ================================================================
// ROI 区域结构体（Qt 进程私有，与主进程 DeviceROI 对齐，坐标为画面像素）
// ================================================================
struct RoiRect {
    int streamId;       // 流 ID
    int deviceId;       // 设备编号（>=1）
    int x, y, w, h;     // 左上角坐标 + 宽高
};

#endif
