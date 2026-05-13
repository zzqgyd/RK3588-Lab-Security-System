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
#define SOCK_PATH_MAIN_QT   "/tmp/sock_main_qt"      // 主进程 → QT（监控画面）
#define SOCK_PATH_FACE_QT   "/tmp/sock_face_qt"      // 人脸进程 → QT（USB画面）
#define SOCK_PATH_QT_FACE   "/tmp/sock_qt_face"      // QT → 人脸进程（命令）
#define SOCK_PATH_MAIN_FACE "/tmp/sock_main_face"    // 主进程 → 人脸进程（事件）
#define SOCK_PATH_MAIN_ROI  "/tmp/sock_main_roi"     // 主进程(服务器) ← QT（ROI重载请求）

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