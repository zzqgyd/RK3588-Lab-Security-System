// ================================================================
// MqttClient 实现
// ----------------------------------------------------------------
// 基于 paho-mqtt-cpp 的异步客户端封装。
// 连接循环在一个独立线程里跑，断线自动重连。
// ================================================================

#include "mqtt_client.h"
#include <cstdio>
#include <chrono>

MqttClient::MqttClient(const std::string& broker_url, const std::string& client_id)
    : broker_url_(broker_url), client_id_(client_id)
{
    client_ = new mqtt::async_client(broker_url_, client_id_);
}

MqttClient::~MqttClient()
{
    stop();
    delete client_;
    client_ = nullptr;
}

// ================================================================
// start：启动连接线程
// ================================================================
bool MqttClient::start()
{
    if (running_.exchange(true)) {
        return true;  // 已启动
    }
    thread_ = std::thread([this] { connectLoop(); });
    return true;
}

// ================================================================
// stop：停止并等待线程退出
// ================================================================
void MqttClient::stop()
{
    if (!running_.exchange(false)) return;

    try {
        if (client_ && connected_.load()) {
            client_->disconnect()->wait();
        }
    } catch (const mqtt::exception& e) {
        fprintf(stderr, "[MqttClient] disconnect 异常: %s\n", e.what());
    }
    connected_.store(false);

    if (thread_.joinable()) thread_.join();
    printf("[MqttClient] 已停止\n");
}

// ================================================================
// setMessageCallback：注册消息回调
// ================================================================
void MqttClient::setMessageCallback(MessageCallback cb)
{
    std::lock_guard<std::mutex> lk(cb_mutex_);
    callback_ = cb;
}

// ================================================================
// publish：发布消息
// ================================================================
bool MqttClient::publish(const std::string& topic, const std::string& payload, int qos)
{
    if (!connected_.load()) {
        fprintf(stderr, "[MqttClient] 未连接，无法发布 %s\n", topic.c_str());
        return false;
    }
    try {
        auto msg = mqtt::make_message(topic, payload);
        msg->set_qos(qos);
        client_->publish(msg);
        return true;
    } catch (const mqtt::exception& e) {
        fprintf(stderr, "[MqttClient] publish 失败: %s\n", e.what());
        return false;
    }
}

// ================================================================
// connectLoop：连接循环（断线自动重连）
// ----------------------------------------------------------------
// 使用 paho 的自动重连机制，但用一个线程来发起首次连接 + 等待。
// ================================================================
void MqttClient::connectLoop()
{
    mqtt::connect_options opts;
    opts.set_keep_alive_interval(20);
    opts.set_clean_session(true);
    opts.set_automatic_reconnect(true);
    opts.set_connect_timeout(5);

    // 遗嘱消息：device_process 掉线时通知 ESP32
    mqtt::message will_msg("device/rk3588/status", "offline", 1, true);
    opts.set_will(will_msg);

    // 设置消息回调
    client_->set_message_callback(
        [this](mqtt::const_message_ptr msg) { this->handleMessage(msg); });

    while (running_.load()) {
        try {
            if (!client_->is_connected()) {
                printf("[MqttClient] 正在连接 %s ...\n", broker_url_.c_str());
                client_->connect(opts)->wait();
                connected_.store(true);
                printf("[MqttClient] 连接成功: %s\n", broker_url_.c_str());

                // 上线通知
                try {
                    auto online_msg = mqtt::make_message("device/rk3588/status", "online");
                    online_msg->set_qos(1);
                    online_msg->set_retained(true);
                    client_->publish(online_msg);
                } catch (...) {}

                // 订阅 ESP32 上行 topic
                subscribeAll();
            }
        } catch (const mqtt::exception& e) {
            fprintf(stderr, "[MqttClient] 连接失败: %s，5秒后重试\n", e.what());
        }

        // 等待 5 秒再检查连接状态
        for (int i = 0; i < 50 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// ================================================================
// subscribeAll：订阅所有 ESP32 上行 topic
// ----------------------------------------------------------------
// 使用通配符 + 订阅：
//   room/+/req/#       —— ESP32 主动请求（识别/终止）
//   room/+/event/#     —— ESP32 事件（ready 推流确认）
//   room/+/heartbeat   —— ESP32 心跳
// ================================================================
void MqttClient::subscribeAll()
{
    try {
        client_->subscribe("room/+/req/#", 1)->wait();
        client_->subscribe("room/+/event/#", 1)->wait();
        client_->subscribe("room/+/heartbeat", 1)->wait();
        printf("[MqttClient] 已订阅: room/+/req/#, room/+/event/#, room/+/heartbeat\n");
    } catch (const mqtt::exception& e) {
        fprintf(stderr, "[MqttClient] 订阅失败: %s\n", e.what());
    }
}

// ================================================================
// handleMessage：内部消息处理，转发给用户回调
// ================================================================
void MqttClient::handleMessage(mqtt::const_message_ptr msg)
{
    std::string topic   = msg->get_topic();
    std::string payload = msg->to_string();

    MessageCallback cb;
    {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        cb = callback_;
    }
    if (cb) {
        cb(topic, payload);
    }
}
