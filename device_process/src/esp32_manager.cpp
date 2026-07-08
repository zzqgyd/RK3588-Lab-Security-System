// ================================================================
// Esp32Manager 实现
// ----------------------------------------------------------------
// 负责所有 ESP32 相关逻辑：MQTT 收发、任务调度、心跳、超时
// ================================================================

#include "esp32_manager.h"
#include "device_manager.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

static int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static int64_t now_sec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

Esp32Manager::Esp32Manager()
    : mqtt_("tcp://127.0.0.1:1883", "device_process")
{
}

Esp32Manager::~Esp32Manager()
{
    stop();
}

// ================================================================
// init：打开 DB
// ================================================================
bool Esp32Manager::init(void* db)
{
    db_ = db;
    return db_ != nullptr;
}

// ================================================================
// start：启动 MQTT + 检测线程
// ================================================================
bool Esp32Manager::start()
{
    if (running_.exchange(true)) return true;

    // 注册 MQTT 消息回调
    mqtt_.setMessageCallback(
        [this](const std::string& topic, const std::string& payload) {
            this->onMqttMessage(topic, payload);
        });

    if (!mqtt_.start()) {
        fprintf(stderr, "[Esp32Manager] MQTT 启动失败\n");
        running_.store(false);
        return false;
    }

    check_thread_ = std::thread([this] { checkLoop(); });

    printf("[Esp32Manager] 已启动\n");
    return true;
}

// ================================================================
// stop
// ================================================================
void Esp32Manager::stop()
{
    if (!running_.exchange(false)) return;
    mqtt_.stop();
    if (check_thread_.joinable()) check_thread_.join();
    printf("[Esp32Manager] 已停止\n");
}

// ================================================================
// setEventCallback
// ================================================================
void Esp32Manager::setEventCallback(Esp32EventCallback cb)
{
    std::lock_guard<std::mutex> lk(cb_mutex_);
    event_cb_ = cb;
}

// ================================================================
// notifyMain：通过回调通知 main_process
// ================================================================
void Esp32Manager::notifyMain(const Esp32EventMsg& msg)
{
    Esp32EventCallback cb;
    {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        cb = event_cb_;
    }
    if (cb) cb(msg);
}

// ================================================================
// publishCmd：给 ESP32 下发命令
// topic 格式: room/{room_id}/cmd/{sub_topic}
// ================================================================
void Esp32Manager::publishCmd(int room_id, const std::string& sub_topic,
                              const std::string& payload)
{
    std::string topic = "room/" + std::to_string(room_id) + "/cmd/" + sub_topic;
    mqtt_.publish(topic, payload);
}

// ================================================================
// onPassiveRegister：被动登记入口（main → device）
// ----------------------------------------------------------------
// 1. 查 DB 拿 ESP32 的 rtsp_url
// 2. 生成 task_id，创建任务（state=kWaitReady）
// 3. MQTT 通知 ESP32 cmd/recognize
// 4. 等 ESP32 event/ready（30s 超时）
// ================================================================
void Esp32Manager::onPassiveRegister(int room_id, int device_id, int duration_minutes)
{
    // 查 DB 拿 ESP32
    RoomEsp32Row esp32;
    if (db_query_esp32_by_room(db_, room_id, &esp32) != 0) {
        fprintf(stderr, "[Esp32Manager] 房间%d 未配置 ESP32，无法触发被动登记\n", room_id);
        // 直接通知 main 登记失败
        Esp32EventMsg msg;
        msg.type            = Esp32Event::kRegisterAck;
        msg.task_id         = 0;
        msg.room_id         = room_id;
        msg.device_id       = device_id;
        msg.duration_minutes= duration_minutes;
        msg.success         = 0;
        notifyMain(msg);
        return;
    }

    // 创建任务
    int task_id = nextTaskId();
    RecognizeTask task;
    task.task_id          = task_id;
    task.task_type        = ESP32_TASK_REGISTER;
    task.room_id          = room_id;
    task.device_id        = device_id;
    task.duration_minutes = duration_minutes;
    task.rtsp_url         = esp32.rtsp_url;
    task.state            = TaskState::kWaitReady;
    task.create_ts_ms     = now_ms();

    {
        std::lock_guard<std::mutex> lk(task_mutex_);
        tasks_[task_id] = task;
    }

    // MQTT 通知 ESP32
    nlohmann::json j;
    j["task_id"]         = task_id;
    j["type"]            = "register";
    j["device_id"]       = device_id;
    j["duration_minutes"]= duration_minutes;
    publishCmd(room_id, "recognize", j.dump());

    printf("[Esp32Manager] 被动登记任务创建: task=%d 房间%d 设备%d %d分钟\n",
           task_id, room_id, device_id, duration_minutes);
}

// ================================================================
// onRecognizeResult：识别结果回传
// ----------------------------------------------------------------
// face_process 识别完，经 main 中转回来
// 1. 更新任务状态
// 2. 成功：
//    - 登记：开插座 + 计时
//    - 签到/签退：写考勤（face_process 已写，这里不重复）
// 3. MQTT 回 ESP32 result
// ================================================================
void Esp32Manager::onRecognizeResult(const Esp32RecognizeResult& result)
{
    int task_id = result.task_id;

    // 取出任务
    RecognizeTask task;
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(task_mutex_);
        auto it = tasks_.find(task_id);
        if (it != tasks_.end()) {
            task = it->second;
            it->second.state = TaskState::kDone;
            found = true;
        }
    }

    if (!found) {
        fprintf(stderr, "[Esp32Manager] 收到未知 task_id=%d 的结果\n", task_id);
        return;
    }

    // 构造给 ESP32 的结果 JSON
    nlohmann::json j;
    j["task_id"]   = task_id;
    j["success"]   = result.success ? true : false;
    if (result.success) {
        j["user"]      = result.person_name;
        j["device_id"] = result.device_id;
        if (task.task_type == ESP32_TASK_REGISTER) {
            j["duration"] = result.duration_minutes;
        }
    }
    publishCmd(task.room_id, "result", j.dump());

    // 通知 main 最终结果（main 要设置 silent 状态）
    Esp32EventMsg msg;
    msg.type            = Esp32Event::kRegisterAck;
    msg.task_id         = task_id;
    msg.task_type       = task.task_type;
    msg.room_id         = task.room_id;
    msg.device_id       = task.device_id;
    msg.duration_minutes= task.duration_minutes;
    msg.success         = result.success;
    msg.person_name     = result.person_name;
    notifyMain(msg);

    printf("[Esp32Manager] 任务%d 完成: success=%d user=%s\n",
           task_id, result.success, result.person_name);
}

// ================================================================
// onMqttMessage：MQTT 消息分发
// ================================================================
void Esp32Manager::onMqttMessage(const std::string& topic, const std::string& payload)
{
    // 解析 topic: room/{room_id}/{category}/{action}
    int room_id = parseRoomId(topic);
    if (room_id < 0) return;

    if (topic.find("/heartbeat") != std::string::npos) {
        handleHeartbeat(room_id);
        return;
    }

    if (topic.find("/req/recognize") != std::string::npos) {
        // ESP32 主动请求识别
        try {
            auto j = nlohmann::json::parse(payload);
            std::string type = j.value("type", "");
            int device_id    = j.value("device_id", 0);
            std::string tid  = j.value("task_id", "");

            int task_type = 0;
            if (type == "register")      task_type = ESP32_TASK_REGISTER;
            else if (type == "signin")   task_type = ESP32_TASK_SIGNIN;
            else if (type == "signout")  task_type = ESP32_TASK_SIGNOUT;
            else return;

            handleEsp32Request(room_id, task_type, device_id, tid);
        } catch (const std::exception& e) {
            fprintf(stderr, "[Esp32Manager] req/recognize JSON 解析失败: %s\n", e.what());
        }
        return;
    }

    if (topic.find("/req/terminate") != std::string::npos) {
        try {
            auto j = nlohmann::json::parse(payload);
            int device_id = j.value("device_id", 0);
            handleEsp32Terminate(room_id, device_id);
        } catch (const std::exception& e) {
            fprintf(stderr, "[Esp32Manager] req/terminate JSON 解析失败: %s\n", e.what());
        }
        return;
    }

    if (topic.find("/event/ready") != std::string::npos) {
        try {
            auto j = nlohmann::json::parse(payload);
            std::string tid = j.value("task_id", "");
            std::string rtsp = j.value("rtsp_url", "");
            handleEsp32Ready(room_id, tid, rtsp);
        } catch (const std::exception& e) {
            fprintf(stderr, "[Esp32Manager] event/ready JSON 解析失败: %s\n", e.what());
        }
        return;
    }
}

// ================================================================
// parseRoomId：从 topic "room/{id}/..." 解析 room_id
// ================================================================
int Esp32Manager::parseRoomId(const std::string& topic)
{
    // topic 格式: room/0/heartbeat, room/2/req/recognize 等
    if (topic.size() < 7 || topic.substr(0, 5) != "room/") return -1;
    size_t pos = topic.find('/', 5);
    if (pos == std::string::npos) return -1;
    try {
        return std::stoi(topic.substr(5, pos - 5));
    } catch (...) {
        return -1;
    }
}

// ================================================================
// handleEsp32Request：ESP32 主动请求识别
// ----------------------------------------------------------------
// 1. 查 DB 拿 ESP32 rtsp_url
// 2. 生成 task_id，创建任务
// 3. 通过回调通知 main 转发识别任务给 face_process
//    （ESP32 主动请求不需要再等 ready，因为 ESP32 自己发起的）
// ================================================================
void Esp32Manager::handleEsp32Request(int room_id, int task_type, int device_id,
                                      const std::string& task_id_str)
{
    RoomEsp32Row esp32;
    if (db_query_esp32_by_room(db_, room_id, &esp32) != 0) {
        fprintf(stderr, "[Esp32Manager] 房间%d 未配置 ESP32\n", room_id);
        return;
    }

    int task_id = nextTaskId();
    RecognizeTask task;
    task.task_id          = task_id;
    task.task_type        = task_type;
    task.room_id          = room_id;
    task.device_id        = device_id;
    task.duration_minutes = 15;  // 默认 15 分钟，登记时由 ESP32 指定
    task.rtsp_url         = esp32.rtsp_url;
    task.state            = TaskState::kRecognizing;
    task.create_ts_ms     = now_ms();

    {
        std::lock_guard<std::mutex> lk(task_mutex_);
        tasks_[task_id] = task;
    }

    // 通知 main 转发识别任务
    Esp32EventMsg msg;
    msg.type            = Esp32Event::kRecognizeReq;
    msg.task_id         = task_id;
    msg.task_type       = task_type;
    msg.room_id         = room_id;
    msg.device_id       = device_id;
    msg.duration_minutes= (task_type == ESP32_TASK_REGISTER) ? 15 : 0;
    msg.rtsp_url        = esp32.rtsp_url;
    notifyMain(msg);

    printf("[Esp32Manager] ESP32 主动请求: task=%d type=%d 房间%d 设备%d\n",
           task_id, task_type, room_id, device_id);
}

// ================================================================
// handleEsp32Ready：ESP32 确认推流
// ----------------------------------------------------------------
// 被动登记流程：ESP32 收到 cmd/recognize 后用户按按钮确认 → event/ready
// 此时通知 main 开始拉流识别
// ================================================================
void Esp32Manager::handleEsp32Ready(int room_id, const std::string& task_id_str,
                                    const std::string& rtsp_url)
{
    int task_id = 0;
    try { task_id = std::stoi(task_id_str); } catch (...) { return; }

    RecognizeTask task;
    {
        std::lock_guard<std::mutex> lk(task_mutex_);
        auto it = tasks_.find(task_id);
        if (it == tasks_.end()) {
            fprintf(stderr, "[Esp32Manager] ready: 未知 task_id=%d\n", task_id);
            return;
        }
        if (it->second.state != TaskState::kWaitReady) {
            fprintf(stderr, "[Esp32Manager] ready: task=%d 状态错误\n", task_id);
            return;
        }
        it->second.state = TaskState::kRecognizing;
        task = it->second;
    }

    // 优先用 ready 消息里的 rtsp_url，没有则用任务里保存的
    std::string url = rtsp_url.empty() ? task.rtsp_url : rtsp_url;

    // 通知 main 开始拉流识别
    Esp32EventMsg msg;
    msg.type            = Esp32Event::kRecognizeReq;
    msg.task_id         = task_id;
    msg.task_type       = task.task_type;
    msg.room_id         = task.room_id;
    msg.device_id       = task.device_id;
    msg.duration_minutes= task.duration_minutes;
    msg.rtsp_url        = url;
    notifyMain(msg);

    printf("[Esp32Manager] ESP32 ready: task=%d 房间%d 开始拉流识别\n",
           task_id, room_id);
}

// ================================================================
// handleEsp32Terminate：ESP32 请求终止设备使用
// ----------------------------------------------------------------
// 1. 通知 main 清检测状态（RELEASE）
// 2. device_manager 关插座 + 清计时（在 onRecognizeResult 或 device_ipc_client 处理）
// 3. MQTT 回 ESP32 result
// ================================================================
void Esp32Manager::handleEsp32Terminate(int room_id, int device_id)
{
    printf("[Esp32Manager] ESP32 终止请求: 房间%d 设备%d\n", room_id, device_id);

    // 通知 main 清状态 + 通知 device_manager 关插座
    Esp32EventMsg msg;
    msg.type      = Esp32Event::kRelease;
    msg.room_id   = room_id;
    msg.device_id = device_id;
    notifyMain(msg);

    // MQTT 回 ESP32
    nlohmann::json j;
    j["success"]   = true;
    j["action"]    = "terminated";
    j["device_id"] = device_id;
    publishCmd(room_id, "result", j.dump());
}

// ================================================================
// handleHeartbeat：ESP32 心跳
// ----------------------------------------------------------------
// 更新 DB online + last_seen，更新内存心跳表
// ================================================================
void Esp32Manager::handleHeartbeat(int room_id)
{
    int64_t ts = now_sec();
    {
        std::lock_guard<std::mutex> lk(hb_mutex_);
        heartbeat_map_[room_id] = now_ms();
    }
    db_update_esp32_online(db_, room_id, 1, ts);
}

// ================================================================
// checkLoop：心跳超时 + 任务超时检测
// ================================================================
void Esp32Manager::checkLoop()
{
    while (running_.load()) {
        checkHeartbeatTimeout();
        checkTaskTimeout();
        for (int i = 0; i < 10 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// ================================================================
// checkHeartbeatTimeout：心跳超时检测（60s 未心跳标记离线）
// ================================================================
void Esp32Manager::checkHeartbeatTimeout()
{
    int64_t now = now_ms();
    std::lock_guard<std::mutex> lk(hb_mutex_);
    for (auto& kv : heartbeat_map_) {
        if (now - kv.second > 60000) {
            // 超过 60s 没心跳，标记离线
            db_update_esp32_online(db_, kv.first, 0, now_sec());
        }
    }
}

// ================================================================
// checkTaskTimeout：任务超时检测
// ----------------------------------------------------------------
// 被动登记任务在 kWaitReady 状态超过 30s 则取消
// ================================================================
void Esp32Manager::checkTaskTimeout()
{
    int64_t now = now_ms();
    std::vector<RecognizeTask> timeout_tasks;
    {
        std::lock_guard<std::mutex> lk(task_mutex_);
        for (auto it = tasks_.begin(); it != tasks_.end(); ) {
            if (it->second.state == TaskState::kWaitReady &&
                now - it->second.create_ts_ms > 30000) {
                it->second.state = TaskState::kTimeout;
                timeout_tasks.push_back(it->second);
                it = tasks_.erase(it);
            } else if (it->second.state == TaskState::kDone) {
                // 已完成任务清理（保留 10s 供查询）
                if (now - it->second.create_ts_ms > 600000) {
                    it = tasks_.erase(it);
                } else {
                    ++it;
                }
            } else {
                ++it;
            }
        }
    }

    for (auto& t : timeout_tasks) {
        printf("[Esp32Manager] 任务%d 超时取消: 房间%d 设备%d\n",
               t.task_id, t.room_id, t.device_id);
        // MQTT 通知 ESP32 取消
        nlohmann::json j;
        j["task_id"] = t.task_id;
        j["reason"]  = "timeout";
        publishCmd(t.room_id, "cancel", j.dump());

        // 通知 main 取消登记
        Esp32EventMsg msg;
        msg.type      = Esp32Event::kCancelRegister;
        msg.task_id   = t.task_id;
        msg.room_id   = t.room_id;
        msg.device_id = t.device_id;
        notifyMain(msg);
    }
}

// ================================================================
// listEsp32：查询所有 ESP32 状态
// ================================================================
int Esp32Manager::listEsp32(Esp32StatusItem* out, int max_count)
{
    RoomEsp32Row rows[DEVICE_MAX_ESP32];
    int n = db_query_all_esp32(db_, rows, DEVICE_MAX_ESP32);
    if (n < 0) return 0;
    if (n > max_count) n = max_count;

    for (int i = 0; i < n; ++i) {
        memset(&out[i], 0, sizeof(Esp32StatusItem));
        out[i].room_id   = rows[i].room_id;
        strncpy(out[i].name,     rows[i].name,     sizeof(out[i].name) - 1);
        strncpy(out[i].esp32_ip, rows[i].esp32_ip, sizeof(out[i].esp32_ip) - 1);
        strncpy(out[i].rtsp_url, rows[i].rtsp_url, sizeof(out[i].rtsp_url) - 1);
        out[i].online    = rows[i].online;
        out[i].last_seen = rows[i].last_seen;
    }
    return n;
}

// ================================================================
// addEsp32 / removeEsp32
// ================================================================
int Esp32Manager::addEsp32(int room_id, const std::string& name,
                           const std::string& ip, const std::string& rtsp_url)
{
    return db_insert_room_esp32(db_, room_id, name.c_str(), ip.c_str(), rtsp_url.c_str());
}

bool Esp32Manager::removeEsp32(int room_id)
{
    return db_delete_room_esp32(db_, room_id) == 0;
}
