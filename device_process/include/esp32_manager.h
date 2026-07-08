#ifndef ESP32_MANAGER_H
#define ESP32_MANAGER_H

#include "mqtt_client.h"
#include "ipc/ipc_socket.h"
#include "ipc/db.h"
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>

class DeviceManager;  // 前向声明

// ================================================================
// Esp32Manager：ESP32 识别任务调度 + 心跳管理
// ----------------------------------------------------------------
// 职责：
//   1. 持有 MqttClient，收发 MQTT 消息
//   2. 维护 ESP32 在线状态（心跳超时检测）
//   3. 调度识别任务：
//      - 被动登记：main → device → MQTT 通知 ESP32 → 等 ready
//                  → 通知 main 拉流识别 → 结果回 ESP32
//      - 主动操作：ESP32 → MQTT req → device → 通知 main 拉流
//                  → 结果回 ESP32
//      - 提前终止：ESP32 → MQTT req/terminate → device 关插座+清计时
//                  → 通知 main 清检测状态
//   4. 30s 超时取消机制（被动登记时 ESP32 未确认）
//
// 与 main_process 的 IPC：
//   - 通过 device_ipc_client 的 main 连接收发 Esp32RecognizeEvent/Result
//   - device → main 的识别请求通过回调上抛给 device_ipc_client 转发
// ================================================================

// 识别任务状态
enum class TaskState {
    kWaitReady = 0,   // 已通知 ESP32，等用户按按钮确认推流
    kRecognizing,     // ESP32 已 ready，face_process 正在识别
    kDone,            // 已完成（结果已回传）
    kTimeout,         // 超时取消
};

// 识别任务上下文
struct RecognizeTask {
    int         task_id;
    int         task_type;        // ESP32_TASK_*
    int         room_id;
    int         device_id;
    int         duration_minutes;
    std::string rtsp_url;
    TaskState   state;
    int64_t     create_ts_ms;     // 任务创建时间（ms）
};

// 上抛给 device_ipc_client 的事件
enum class Esp32Event {
    kRecognizeReq = 0,   // 请求 main 转发识别任务给 face_process
    kRegisterAck,        // 识别完成，通知 main 是否登记成功
    kRelease,            // ESP32 请求终止，通知 main 清检测状态
    kCancelRegister,     // 30s 超时，通知 main 取消登记
};

struct Esp32EventMsg {
    Esp32Event          type;
    int                 task_id;
    int                 task_type;
    int                 room_id;
    int                 device_id;
    int                 duration_minutes;
    int                 success;        // kRegisterAck 用
    std::string         person_name;    // kRegisterAck 用
    std::string         rtsp_url;       // kRecognizeReq 用
};

// 事件回调：Esp32Manager → device_ipc_client（转发给 main_process）
using Esp32EventCallback = std::function<void(const Esp32EventMsg&)>;

class Esp32Manager {
public:
    Esp32Manager();
    ~Esp32Manager();

    // 禁拷贝
    Esp32Manager(const Esp32Manager&) = delete;
    Esp32Manager& operator=(const Esp32Manager&) = delete;

    // 初始化：打开 DB
    bool init(void* db);

    // 启动：启动 MQTT + 心跳检测 + 任务超时检测线程
    bool start();

    // 停止
    void stop();

    // 注册事件回调（上抛给 device_ipc_client）
    void setEventCallback(Esp32EventCallback cb);

    // ===== 被外部（device_ipc_client）调用的接口 =====

    // main_process 发来 REGISTER 事件：触发被动登记流程
    //   1. 查 DB 拿 ESP32 的 rtsp_url
    //   2. MQTT 通知 ESP32 cmd/recognize
    //   3. 等 ESP32 event/ready
    //   4. ready 后通过回调通知 main 拉流识别
    //   5. 30s 超时未 ready 则取消
    void onPassiveRegister(int room_id, int device_id, int duration_minutes);

    // face_process 识别结果回传（经 main_process 中转）
    //   1. 写考勤/设备使用记录
    //   2. 成功则通知 device_manager 开插座+计时
    //   3. MQTT 回 ESP32 result
    void onRecognizeResult(const Esp32RecognizeResult& result);

    // ===== ESP32 管理（供 device_ipc_client 调用）=====

    // 查询所有 ESP32 状态（供 QT 显示）
    int listEsp32(Esp32StatusItem* out, int max_count);

    // 新增 ESP32
    int addEsp32(int room_id, const std::string& name,
                 const std::string& ip, const std::string& rtsp_url);

    // 删除 ESP32
    bool removeEsp32(int room_id);

private:
    // MQTT 消息回调
    void onMqttMessage(const std::string& topic, const std::string& payload);

    // 解析 topic 中的 room_id（room/{id}/...）
    static int parseRoomId(const std::string& topic);

    // 心跳检测 + 任务超时检测线程
    void checkLoop();

    // 生成 task_id（自增）
    int nextTaskId() { return ++task_id_counter_; }

    // 通知 main_process（通过回调）
    void notifyMain(const Esp32EventMsg& msg);

    // 处理 ESP32 主动请求（signin/signout/register）
    void handleEsp32Request(int room_id, int task_type, int device_id,
                            const std::string& task_id_str);

    // 处理 ESP32 ready 事件
    void handleEsp32Ready(int room_id, const std::string& task_id_str,
                          const std::string& rtsp_url);

    // 处理 ESP32 终止请求
    void handleEsp32Terminate(int room_id, int device_id);

    // 处理 ESP32 心跳
    void handleHeartbeat(int room_id);

    // MQTT publish 辅助
    void publishCmd(int room_id, const std::string& sub_topic,
                    const std::string& payload);

    // 30s 超时取消检查
    void checkTaskTimeout();

    // 心跳超时检查（60s 未心跳标记离线）
    void checkHeartbeatTimeout();

private:
    void*               db_ = nullptr;
    MqttClient          mqtt_;
    Esp32EventCallback  event_cb_;
    std::mutex          cb_mutex_;

    // 任务表（task_id → task）
    std::mutex          task_mutex_;
    std::map<int, RecognizeTask> tasks_;

    // task_id 自增计数器
    std::atomic<int>    task_id_counter_{0};

    // 检测线程
    std::thread         check_thread_;
    std::atomic<bool>   running_{false};

    // 心跳记录（room_id → 最后心跳时间戳 ms）
    std::mutex          hb_mutex_;
    std::map<int, int64_t> heartbeat_map_;
};

#endif // ESP32_MANAGER_H
