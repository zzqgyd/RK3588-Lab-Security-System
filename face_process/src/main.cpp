/**
 * @file face_service/src/main.cpp
 * @brief 人脸识别服务 — Socket 客户端模式
 *
 * 通信方式（QT 创建所有服务器，人脸进程作为客户端连接）：
 *   1. SOCK_PATH_FACE_QT : 连接 QT（推送 USB 摄像头画面）
 *   2. SOCK_PATH_QT_FACE : 连接 QT（接收签到/签退/录入/删除命令）
 *   3. SOCK_PATH_MAIN_FACE: 连接主进程（接收设备区域事件）
 *
 * 启动顺序：
 *   1. QT 先启动（创建所有服务器）
 *   2. 主进程启动（创建 sock_main_face 服务器）
 *   3. 人脸进程最后启动（连接所有服务器）
 */

#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <chrono>   // ★ 新增：用于2秒计时

#include "face_module.h"
#include "v4l2_camera.h"
#include "recognition_engine.hpp"
#include "communication_layer.hpp"
#include "ipc/ipc_socket.h"
#include "ipc/db.h"
#include <inspireface/inspireface.hpp>

// ============================================================
// 全局退出标志
// ============================================================
volatile bool g_running = true;

static void sigint_handler(int) {
    g_running = false;
    printf("\n[FaceService] 退出信号\n");
}

// ============================================================
// Socket 连接变量（全部作为客户端）
// ============================================================
static int g_qt_face_socket = -1;
static int g_qt_cmd_socket = -1;
static int g_main_face_socket = -1;

// ============================================================
// 全局指针（供线程使用）
// ============================================================
static FaceModule* g_face = nullptr;
static RecognitionEngine* g_engine = nullptr;
static CommunicationLayer* g_comm = nullptr;
static V4L2Camera* g_cam = nullptr;

// ============================================================
// 辅助函数：设置非阻塞
// ============================================================
static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

// ================================================================
// ★ 新增：人脸录入专用函数（前2秒显示画面，2秒后采集一帧录入）
// ================================================================
static int do_face_enroll(const char* person_name, int& out_feature_id, bool& is_duplicate)
{
    printf("[FaceService] 录入开始: %s (前2秒预览，2秒后采集)\n", person_name);
    
    // ============ 第一步：前2秒只采集和推流，不做推理 ============
    auto start_time = std::chrono::steady_clock::now();
    const int PREVIEW_SECONDS = 2;
    int frame_count = 0;
    
    while (g_running) {
        auto elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - start_time).count();
        
        if (elapsed >= PREVIEW_SECONDS) {
            break;  // 2秒结束
        }
        
        // 采集一帧
        uint8_t* data;
        size_t size;
        int w, h, sw, sh;
        if (g_cam->capture(&data, &size, &w, &h, &sw, &sh) != 0) {
            usleep(10000);
            continue;
        }
        int idx = g_cam->current_buffer_index();
        
        // ★ 预览期间：只推流到QT显示，不做人脸识别
        // 使用 RecognitionEngine 的 DisplaySender 推流
        // 但我们无法直接访问 engine 的 sender，所以这里通过 ipc 推流
        
        // 导出 DMA-BUF fd 推流到 QT
        int fd = g_cam->export_dma_fd();
        if (fd >= 0 && g_qt_face_socket >= 0) {
            FrameMeta meta;
            memset(&meta, 0, sizeof(meta));
            meta.width = w;
            meta.height = h;
            meta.stride_w = sw;
            meta.stride_h = sh;
            meta.format = 1;  // YUYV
            meta.size = size;
            meta.stream_id = -1;
            meta.detection_count = 0;
            
            ipc_sock_send_frame(g_qt_face_socket, &meta, fd);
            close(fd);
        }
        
        // 归还缓冲区
        g_cam->qbuf(idx);
        frame_count++;
        
        // 每秒打印一次状态
        if (frame_count % 30 == 0) {
            printf("[FaceService] 预览中... %.1f/2s\n", elapsed);
        }
    }
    
    printf("[FaceService] 预览结束，采集最后一帧进行录入\n");
    
    // ============ 第二步：采集当前帧进行录入 ============
    uint8_t* data;
    size_t size;
    int w, h, sw, sh;
    
    // 最多等待3秒获取一帧
    int retry = 0;
    while (g_running && retry < 30) {
        if (g_cam->capture(&data, &size, &w, &h, &sw, &sh) == 0) {
            break;
        }
        usleep(100000);
        retry++;
    }
    
    if (retry >= 30) {
        printf("[FaceService] 采集失败: 超时\n");
        return -1;
    }
    
    int idx = g_cam->current_buffer_index();

    // 构建 image_buffer_t
    // ★ 为 RGA 导出 DMA-BUF fd（比 virt_addr 路径更稳定）
    int rga_fd = g_cam->export_dma_fd();
    image_buffer_t img;
    img.virt_addr = data;
    img.width = w;
    img.height = h;
    img.width_stride = sw;
    img.height_stride = sh;
    img.format = IMAGE_FORMAT_YUYV422;
    img.size = size;
    img.fd = rga_fd;

    // ============ 第三步：执行录入（特征提取 + 去重） ============
    int fid;
    bool dup;
    int ret = g_face->extract_dedup(&img, fid, dup);

    // 释放 RGA fd + 归还缓冲区
    if (rga_fd >= 0) close(rga_fd);
    g_cam->qbuf(idx);
    
    if (ret == 0) {
        out_feature_id = fid;
        is_duplicate = dup;
        if (dup) {
            char exist_name[64];
            g_comm->db_query_name(fid, exist_name, sizeof(exist_name));
            printf("[FaceService] 重复录入: %s (feature_id=%d)\n", exist_name, fid);
        } else {
            printf("[FaceService] 录入成功: %s (feature_id=%d)\n", person_name, fid);
        }
        return 0;
    }
    
    printf("[FaceService] 录入失败: 未检测到人脸\n");
    return -1;
}

// ================================================================
// 处理 QT 命令的线程
// ================================================================
static void* qt_command_handler(void* arg) {
    FaceCommand cmd;
    
    printf("[FaceService] QT command handler started\n");
    
    while (g_running) {
        if (g_qt_cmd_socket < 0) {
            usleep(100000);
            continue;
        }
        
        ssize_t n = recv(g_qt_cmd_socket, &cmd, sizeof(cmd), 0);
        if (n <= 0) {
            if (n == 0) {
                printf("[FaceService] QT command connection closed\n");
                close(g_qt_cmd_socket);
                g_qt_cmd_socket = -1;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("[FaceService] recv from QT");
            }
            usleep(10000);
            continue;
        }
        
        if (n != sizeof(cmd)) {
            printf("[FaceService] Invalid command size: %ld\n", n);
            continue;
        }
        
        printf("[FaceService] Received command: mode=%d\n", cmd.mode);
        
        SessionResult res;
        memset(&res, 0, sizeof(res));
        
        switch (cmd.mode) {
            case 0:
            case 1: {
                bool ok = g_engine->run(0, res);

                if (ok) {
                    char name[64];
                    int room_id = 0;
                    // 查询人名 + 所属房间号，把房间号一并写入 attendance
                    if (g_comm->db_query_name_ex(res.feature_id, name, sizeof(name), &room_id) != 0) {
                        snprintf(name, sizeof(name), "陌生人(ID_%d)", res.feature_id);
                        room_id = 0;
                    }

                    int ret = g_comm->db_write_attendance(name,
                                (cmd.mode == 0) ? "签到" : "签退", room_id);
                    if (ret == -2) {
                        cmd.result = -5;
                    } else if (ret == 0) {
                        cmd.result = 0;
                    } else {
                        cmd.result = -1;
                    }
                    snprintf(cmd.reply_name, sizeof(cmd.reply_name), "%s", name);
                } else {
                    cmd.result = -2;
                    snprintf(cmd.reply_name, sizeof(cmd.reply_name), "超时");
                }
                break;
            }

            case 2: {
                bool ok = g_engine->run(0, res);

                if (ok) {
                    char name[64];
                    if (g_comm->db_query_name(res.feature_id, name, sizeof(name)) != 0) {
                        snprintf(name, sizeof(name), "陌生人(ID_%d)", res.feature_id);
                    }
                    g_comm->db_write_device_usage(name, cmd.room_id,
                                                   cmd.device_id, cmd.duration_minutes);
                    cmd.result = 0;
                    snprintf(cmd.reply_name, sizeof(cmd.reply_name), "%s", name);
                } else {
                    cmd.result = -2;
                    snprintf(cmd.reply_name, sizeof(cmd.reply_name), "超时");
                }
                break;
            }

            // ============================================================
            // ★ case 3: 人脸录入 — 使用新的2秒预览逻辑
            // ============================================================
            case 3: {
                printf("[FaceService] 录入命令: %s (房间%d)\n", cmd.person_name, cmd.room_id);

                // 检查人名是否已存在
                int exist_id;
                if (g_comm->db_check_name_exists(cmd.person_name, exist_id)) {
                    cmd.result = -3;
                    snprintf(cmd.reply_name, sizeof(cmd.reply_name), "%s", cmd.person_name);
                    printf("[FaceService] 重复人名: %s (feature_id=%d)\n",
                           cmd.person_name, exist_id);
                    break;
                }

                // ========== ★ 调用新的录入函数 ==========
                int fid;
                bool dup;
                int ret = do_face_enroll(cmd.person_name, fid, dup);

                if (ret == 0) {
                    if (dup) {
                        char exist_name[64];
                        g_comm->db_query_name(fid, exist_name, sizeof(exist_name));
                        cmd.result = -3;
                        snprintf(cmd.reply_name, sizeof(cmd.reply_name), "%s", exist_name);
                        printf("[FaceService] 人脸特征已存在: %s (feature_id=%d)\n",
                               exist_name, fid);
                    } else {
                        g_comm->db_insert_mapping(fid, cmd.person_name, cmd.room_id);
                        cmd.result = 0;
                        snprintf(cmd.reply_name, sizeof(cmd.reply_name), "%s", cmd.person_name);
                        printf("[FaceService] 录入成功: %s (feature_id=%d, 房间%d)\n",
                               cmd.person_name, fid, cmd.room_id);
                    }
                } else {
                    cmd.result = -1;
                    snprintf(cmd.reply_name, sizeof(cmd.reply_name), "未检测到人脸");
                }
                break;
            }
            
            case 4: {
                printf("[FaceService] 删除: feature_id=%d\n", cmd.feature_id);
                INSPIREFACE_FEATURE_HUB->FaceFeatureRemove(cmd.feature_id);
                g_comm->db_delete_mapping(cmd.feature_id);
                cmd.result = 0;
                snprintf(cmd.reply_name, sizeof(cmd.reply_name), "删除成功");
                break;
            }
            
            default:
                cmd.result = -1;
                snprintf(cmd.reply_name, sizeof(cmd.reply_name), "未知命令");
                break;
        }
        
        // 发送结果到 QT
        send(g_qt_cmd_socket, &cmd, sizeof(cmd), 0);
        printf("[FaceService] Replied to QT: result=%d, name=%s\n", 
               cmd.result, cmd.reply_name);
    }
    
    return NULL;
}

// ================================================================
// 处理主进程 ESP32 识别事件的线程
// ----------------------------------------------------------------
// USB 被动识别已废弃，主进程通过 SOCK_PATH_MAIN_FACE 下发
// Esp32RecognizeEvent（含 rtsp_url），face_process 拉流识别后
// 回传 Esp32RecognizeResult。
//
// 流程：
//   main → face:  Esp32RecognizeEvent（task_id, type, room, device, rtsp_url）
//   face → main:  Esp32RecognizeResult（task_id, success, name, ...）
//
// task_type:
//   ESP32_TASK_REGISTER(1): 设备使用登记 → 成功写 device_usage
//   ESP32_TASK_SIGNIN(2):   主动签到 → 成功写 attendance
//   ESP32_TASK_SIGNOUT(3):  主动签退 → 成功写 attendance
// ================================================================
static void* main_event_handler(void* arg) {
    printf("[FaceService] ESP32 event handler started\n");

    while (g_running) {
        if (g_main_face_socket < 0) {
            usleep(100000);
            continue;
        }

        Esp32RecognizeEvent ev;
        memset(&ev, 0, sizeof(ev));
        ssize_t n = recv(g_main_face_socket, &ev, sizeof(ev), 0);
        if (n <= 0) {
            if (n == 0) {
                printf("[FaceService] Main process connection closed\n");
                close(g_main_face_socket);
                g_main_face_socket = -1;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("[FaceService] recv from main");
            }
            usleep(10000);
            continue;
        }

        if (n != sizeof(ev)) {
            printf("[FaceService] 包长异常: %zd (期望 %zu)\n", n, sizeof(ev));
            continue;
        }

        printf("[FaceService] ESP32 识别请求: task=%d type=%d 房间%d 设备%d %s\n",
               ev.task_id, ev.task_type, ev.room_id, ev.device_id, ev.rtsp_url);

        // 调用流识别（OpenCV 拉流 + search_bgr）
        char name[64] = {0};
        int  feature_id = 0;
        bool ok = g_engine->recognize_from_stream(ev, name, feature_id);

        // 构造回传结果
        Esp32RecognizeResult result;
        memset(&result, 0, sizeof(result));
        result.task_id          = ev.task_id;
        result.room_id          = ev.room_id;
        result.device_id        = ev.device_id;
        result.duration_minutes = ev.duration_minutes;

        if (ok) {
            result.success    = 1;
            result.feature_id = feature_id;

            // 查人名（recognize_from_stream 已查，但这里再确认一次）
            char qname[64] = {0};
            if (g_comm->db_query_name(feature_id, qname, sizeof(qname)) == 0) {
                snprintf(result.person_name, sizeof(result.person_name), "%s", qname);
            } else {
                snprintf(result.person_name, sizeof(result.person_name),
                         "陌生人(ID_%d)", feature_id);
            }

            // 根据 task_type 写业务记录
            int room_for_log = ev.room_id;
            if (ev.task_type == ESP32_TASK_REGISTER) {
                // 设备使用登记
                g_comm->db_write_device_usage(result.person_name,
                                              ev.room_id, ev.device_id,
                                              ev.duration_minutes);
                printf("[FaceService] 设备登记: %s 房间%d 设备%d %d分钟\n",
                       result.person_name, ev.room_id, ev.device_id, ev.duration_minutes);
            } else if (ev.task_type == ESP32_TASK_SIGNIN) {
                g_comm->db_write_attendance(result.person_name, "签到", room_for_log);
                printf("[FaceService] 签到: %s 房间%d\n", result.person_name, room_for_log);
            } else if (ev.task_type == ESP32_TASK_SIGNOUT) {
                g_comm->db_write_attendance(result.person_name, "签退", room_for_log);
                printf("[FaceService] 签退: %s 房间%d\n", result.person_name, room_for_log);
            }
        } else {
            result.success = 0;
            printf("[FaceService] 流识别失败/超时 task=%d\n", ev.task_id);
        }

        // 回传给主进程
        send(g_main_face_socket, &result, sizeof(result), 0);
        printf("[FaceService] → main: 识别结果 task=%d success=%d\n",
               result.task_id, result.success);
    }

    return NULL;
}

// ================================================================
// 主函数
// ================================================================
int main(int argc, char* argv[]) {
    signal(SIGINT, sigint_handler);
    printf("[FaceService] 启动（Socket 客户端模式）\n");

    // --------------------------------------------------------
    // 1. 初始化人脸识别模块
    // --------------------------------------------------------
    FaceModule face;
    if (face.init("./model/Gundam_RK3588", "../../config/face_database.db") != 0) {
        fprintf(stderr, "[FaceService] 人脸模块初始化失败\n");
        return -1;
    }
    g_face = &face;

    // --------------------------------------------------------
    // 2. 打开业务数据库
    // --------------------------------------------------------
    void* db = db_open("../../config/records.db");
    if (!db) {
        fprintf(stderr, "[FaceService] 数据库打开失败\n");
        return -1;
    }

    // --------------------------------------------------------
    // 3. 打开 USB 摄像头
    // --------------------------------------------------------
    V4L2Camera cam;
    if (cam.start("/dev/video21", 640, 360, 30) != 0) {
        fprintf(stderr, "[FaceService] 摄像头打开失败\n");
        return -1;
    }
    g_cam = &cam;

    // ================================================================
    // 4. 连接 QT 的服务器（sock_face_qt，用于推送 USB 画面）
    // ================================================================
    g_qt_face_socket = ipc_sock_client_connect(SOCK_PATH_FACE_QT);
    if (g_qt_face_socket < 0) {
        printf("[FaceService] Warning: Cannot connect to QT (sock_face_qt)\n");
    } else {
        printf("[FaceService] Connected to QT: %s\n", SOCK_PATH_FACE_QT);
    }

    // ================================================================
    // 5. 连接 QT 的服务器（sock_qt_face，用于接收命令）
    // ================================================================
    g_qt_cmd_socket = ipc_sock_client_connect(SOCK_PATH_QT_FACE);
    if (g_qt_cmd_socket < 0) {
        printf("[FaceService] Warning: Cannot connect to QT (sock_qt_face)\n");
    } else {
        printf("[FaceService] Connected to QT: %s\n", SOCK_PATH_QT_FACE);
        set_nonblocking(g_qt_cmd_socket);
    }

    // ================================================================
    // 6. 连接主进程的服务器（sock_main_face，用于接收设备事件）
    // ================================================================
    g_main_face_socket = ipc_sock_client_connect(SOCK_PATH_MAIN_FACE);
    if (g_main_face_socket < 0) {
        printf("[FaceService] Warning: Cannot connect to main process\n");
    } else {
        printf("[FaceService] Connected to main process: %s\n", SOCK_PATH_MAIN_FACE);
        set_nonblocking(g_main_face_socket);
    }

    // --------------------------------------------------------
    // 7. 创建通信层和推理引擎
    // --------------------------------------------------------
    CommunicationLayer comm(db);
    g_comm = &comm;
    
    RecognitionEngine engine(face, cam, g_qt_face_socket);
    g_engine = &engine;

    // --------------------------------------------------------
    // 8. 启动处理线程
    // --------------------------------------------------------
    pthread_t cmd_thread, event_thread;
    pthread_create(&cmd_thread, NULL, qt_command_handler, NULL);
    pthread_create(&event_thread, NULL, main_event_handler, NULL);

    printf("[FaceService] 就绪\n");

    // --------------------------------------------------------
    // 9. 主循环（只需要保持运行）
    // --------------------------------------------------------
    while (g_running) {
        usleep(100000);
    }

    // --------------------------------------------------------
    // 10. 清理资源
    // --------------------------------------------------------
    printf("[FaceService] 清理...\n");
    
    pthread_cancel(cmd_thread);
    pthread_cancel(event_thread);
    pthread_join(cmd_thread, NULL);
    pthread_join(event_thread, NULL);
    
    cam.stop();
    db_close(db);
    face.deinit();

    if (g_qt_face_socket >= 0) close(g_qt_face_socket);
    if (g_qt_cmd_socket >= 0) close(g_qt_cmd_socket);
    if (g_main_face_socket >= 0) close(g_main_face_socket);

    printf("[FaceService] 退出\n");
    return 0;
}