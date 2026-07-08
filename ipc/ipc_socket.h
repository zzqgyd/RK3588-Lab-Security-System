#ifndef IPC_SOCKET_H
#define IPC_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ================================================================
 * DetectionBox —— IPC 协议层自包含定义
 * 用保护宏避免与各进程 config.h 中的同名定义冲突
 * ================================================================ */
#ifndef IPC_DETECTION_BOX_DEFINED
#define IPC_DETECTION_BOX_DEFINED
struct DetectionBox {
    int x, y, w, h;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Unix Socket 路径
 * ================================================================ */
#define SOCK_PATH_MAIN_QT      "/tmp/sock_main_qt"       // 主进程 → QT（监控画面）
#define SOCK_PATH_FACE_QT      "/tmp/sock_face_qt"       // 人脸进程 → QT（USB画面）
#define SOCK_PATH_QT_FACE      "/tmp/sock_qt_face"       // QT → 人脸进程（命令）
#define SOCK_PATH_MAIN_FACE    "/tmp/sock_main_face"     // 主进程 → 人脸进程（事件）
#define SOCK_PATH_MAIN_ROI     "/tmp/sock_main_roi"      // 主进程(服务器) ← QT（ROI重载请求）
#define SOCK_PATH_QT_DEVICE    "/tmp/sock_qt_device"     // QT ↔ 设备进程（状态查询/管理）
#define SOCK_PATH_MAIN_DEVICE  "/tmp/sock_main_device"   // 主进程 → 设备进程（登记/释放插座）
#define SOCK_PATH_QT_MAIN      "/tmp/sock_qt_main"       // QT → 主进程（手动断电等命令）

/* ================================================================
 * IPC 共用常量（不依赖任何进程的私有 config.h）
 * 注意：以下数值必须与各进程 config.h 中的定义保持一致
 * ================================================================ */
#ifndef IPC_MAX_DETECTIONS
#define IPC_MAX_DETECTIONS  64    /* 与 MAX_DETECTIONS 对齐 */
#endif
#ifndef IPC_MAX_DEVICES_EACH
#define IPC_MAX_DEVICES_EACH 4    /* 与 MAX_DEVICES_EACH 对齐 */
#endif

/* ================================================================
 * 帧元数据
 * ================================================================ */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride_w;
    uint32_t stride_h;
    uint32_t format;
    uint32_t size;
    uint64_t pts;
    int      stream_id;
    uint32_t detection_count;
    struct DetectionBox boxes[IPC_MAX_DETECTIONS];

    /* ===== 设备占用状态（主进程随帧下发，Qt 用于 ROI 框颜色/文字）===== */
    uint32_t device_count;                                  /* 该路设备数 */
    int32_t  device_ids[IPC_MAX_DEVICES_EACH];              /* 对应设备编号 */
    uint32_t device_occupied[IPC_MAX_DEVICES_EACH];         /* 1=使用中(免打扰), 0=空闲 */
} FrameMeta;

/* ================================================================
 * QT ↔ 人脸进程 命令结构体
 * ================================================================ */
typedef struct {
    int cmd_id;
    int mode;                   // 0签到 1签退 2设备登记 3录入 4删除
    int room_id;
    int device_id;
    int duration_minutes;
    char person_name[64];
    int feature_id;
    int result;
    char reply_name[64];
} FaceCommand;

/* ================================================================
 * 主进程 ↔ 人脸进程 事件结构体
 * ================================================================ */
typedef struct {
    int room_id;
    int device_id;
    int duration_minutes;
    int confirm_duration;
} FaceEvent;

/* ================================================================
 * 设备进程 IPC 命令/响应结构体
 * ----------------------------------------------------------------
 * QT ↔ device_process（SOCK_PATH_QT_DEVICE）：
 *   QT 发 DeviceCmd，device_process 回 DeviceStatusResp
 *
 * 主进程 → device_process（SOCK_PATH_MAIN_DEVICE）：
 *   主进程发 DeviceEvent
 * ================================================================ */

/* 设备 IPC 命令类型 */
#define DEVICE_CMD_QUERY_ROOM    1   /* QT 查询指定房间所有插座状态 */
#define DEVICE_CMD_POWER_OFF     2   /* QT 手动断电（等同于提前结束使用） */
#define DEVICE_CMD_ADD_PLUG      3   /* QT 新增插座配置 */
#define DEVICE_CMD_DEL_PLUG      4   /* QT 删除插座配置 */
#define DEVICE_CMD_LIST_ROOMS    5   /* QT 查询哪些房间有插座 */
#define DEVICE_CMD_LIST_ESP32    6   /* QT 查询所有 ESP32 配置 + 在线状态 */
#define DEVICE_CMD_ADD_ESP32     7   /* QT 新增 ESP32 配置 */
#define DEVICE_CMD_DEL_ESP32     8   /* QT 删除 ESP32 配置 */
#define DEVICE_CMD_REGISTER      9   /* QT 主动登记使用（USB识别成功后开插座+倒计时）*/

/* 设备 IPC 事件类型（主进程 → device_process）*/
#define DEVICE_EVENT_REGISTER    1   /* 设备登记成功，打开插座+倒计时 */
#define DEVICE_EVENT_RELEASE     2   /* 设备释放/提前结束，关闭插座 */

/* QT → device_process 的命令 */
typedef struct {
    int cmd;                  /* DEVICE_CMD_* */
    int room_id;
    int device_id;
    int duration_minutes;     /* DEVICE_CMD_ADD_PLUG 时使用 */
    char name[64];            /* DEVICE_CMD_ADD_PLUG / ADD_ESP32 时使用 */
    char ip[64];              /* DEVICE_CMD_ADD_PLUG / ADD_ESP32 时使用 */
    char token[33];           /* DEVICE_CMD_ADD_PLUG 时使用 */
    char rtsp_url[128];       /* DEVICE_CMD_ADD_ESP32 时使用 */
} DeviceCmd;

/* device_process → QT 的状态响应（单个插座）*/
typedef struct {
    int  room_id;
    int  device_id;
    char name[64];
    int  online;              /* 1=在线, 0=离线 */
    int  is_on;               /* 1=开, 0=关 */
    int  fault;               /* 0=无, 1=过温, 2=过载 */
    int  temperature;         /* ℃ */
    int  power_w_x10;         /* 功率 ×10（用整数避免浮点传输）*/
    int  energy_kwh_x100;     /* 累计电量 ×100 */
    int  countdown_left_min;  /* 倒计时剩余分钟 */
    int  on_off_count;        /* 开关次数 */
} DeviceStatusItem;

/* device_process → QT 的房间状态响应（含多个插座）*/
#define DEVICE_MAX_PER_ROOM  8
typedef struct {
    int  count;                                       /* 实际插座数 */
    DeviceStatusItem items[DEVICE_MAX_PER_ROOM];      /* 插座状态数组 */
} DeviceStatusResp;

/* ================================================================
 * ESP32 状态项（用于 QT 显示 ESP32 列表）
 * ================================================================ */
typedef struct {
    int  room_id;
    char name[64];
    char esp32_ip[64];
    char rtsp_url[128];
    int  online;              /* 1=在线, 0=离线 */
    int64_t last_seen;        /* 最后心跳时间戳（秒） */
} Esp32StatusItem;

/* device_process → QT 的 ESP32 列表响应 */
#define DEVICE_MAX_ESP32  8
typedef struct {
    int  count;                                       /* 实际 ESP32 数 */
    Esp32StatusItem items[DEVICE_MAX_ESP32];          /* ESP32 状态数组 */
} Esp32StatusResp;

/* 主进程 → device_process 的事件 */
typedef struct {
    int event;                /* DEVICE_EVENT_* */
    int room_id;
    int device_id;
    int duration_minutes;     /* DEVICE_EVENT_REGISTER 时使用 */
} DeviceEvent;

/* ================================================================
 * QT → 主进程 命令结构体（SOCK_PATH_QT_MAIN）
 * ----------------------------------------------------------------
 * 目前用于"手动断电"——QT 把请求发给主进程，
 * 主进程清空设备状态后转发 RELEASE 给 device_process
 * ================================================================ */
#define MAIN_CMD_POWER_OFF     1   /* QT 手动断电（提前结束使用） */

typedef struct {
    int cmd;                  /* MAIN_CMD_* */
    int room_id;
    int device_id;
} MainCmd;

/* 主进程 → QT 的回执（成功 0，失败 -1）*/
typedef struct {
    int result;               /* 0=成功, -1=失败 */
} MainAck;

/* ================================================================
 * ESP32 人脸识别事件（main_process ↔ face_process）
 * ----------------------------------------------------------------
 * main_process 检测到设备区域有人超阈值后，通知 device_process
 * 走 ESP32 识别流程。device_process 通过 main_process 中转
 * 识别任务给 face_process。
 *
 * 流程：
 *   1. main → device: DEVICE_EVENT_REGISTER（通知有设备需登记）
 *   2. device → main: ESP32_RECOGNIZE_REQ（请求识别，含 rtsp_url）
 *   3. main → face:   Esp32RecognizeEvent（转发给 face_process）
 *   4. face → main:   Esp32RecognizeResult（识别结果）
 *   5. main → device: DEVICE_EVENT_REGISTER_ACK（最终结果）
 * ================================================================ */

/* ESP32 识别请求类型 */
#define ESP32_TASK_REGISTER    1   /* 设备使用登记 */
#define ESP32_TASK_SIGNIN      2   /* 主动签到 */
#define ESP32_TASK_SIGNOUT     3   /* 主动签退 */

/* main_process → face_process：ESP32 识别任务 */
typedef struct {
    int  task_id;             /* 任务ID，用于匹配结果 */
    int  task_type;           /* ESP32_TASK_* */
    int  room_id;             /* 房间号 */
    int  device_id;           /* 设备号（签到/签退时为0） */
    int  duration_minutes;    /* 使用时长（登记时有效） */
    char rtsp_url[128];       /* ESP32 RTSP 流地址 */
} Esp32RecognizeEvent;

/* face_process → main_process：ESP32 识别结果 */
typedef struct {
    int  task_id;             /* 对应请求的 task_id */
    int  success;             /* 1=成功, 0=失败 */
    int  feature_id;          /* 识别到的 feature_id（成功时有效） */
    char person_name[64];     /* 人名 */
    int  room_id;             /* 房间号（回传） */
    int  device_id;           /* 设备号（回传） */
    int  duration_minutes;    /* 使用时长（回传） */
} Esp32RecognizeResult;

/* ================================================================
 * Socket 操作接口
 * ================================================================ */
int  ipc_sock_server_create(const char* path, int backlog);
int  ipc_sock_accept(int listen_fd);
int  ipc_sock_client_connect(const char* path);
int  ipc_sock_send(int sock_fd, const void* data, size_t len);
int  ipc_sock_recv(int sock_fd, void* data, size_t len);
int  ipc_sock_send_frame(int sock_fd, const FrameMeta* meta, int dma_fd);
int  ipc_sock_recv_frame(int sock_fd, FrameMeta* meta, int* dma_fd);
void ipc_sock_close(int fd, const char* path);

#ifdef __cplusplus
}
#endif

#endif // IPC_SOCKET_H