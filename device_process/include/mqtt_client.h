#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <mqtt/async_client.h>

// ================================================================
// MqttClient：device_process 的 MQTT 客户端封装
// ----------------------------------------------------------------
// 基于 paho-mqtt-cpp，负责：
//   1. 连接本地 mosquitto broker（127.0.0.1:1883）
//   2. 订阅 ESP32 上行 topic（room/+/req/#, room/+/event/#, room/+/heartbeat）
//   3. publish 下行命令给 ESP32
//   4. 收到消息后回调上层（Esp32Manager）
//   5. 自动重连
// ================================================================

class MqttClient {
public:
    // 消息回调：topic + payload
    using MessageCallback =
        std::function<void(const std::string& topic, const std::string& payload)>;

    MqttClient(const std::string& broker_url = "tcp://127.0.0.1:1883",
               const std::string& client_id  = "device_process");
    ~MqttClient();

    // 禁拷贝
    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;

    // 启动：连接 broker + 订阅 + 启动消费线程
    bool start();

    // 停止：断开连接 + join 线程
    void stop();

    // 注册消息回调
    void setMessageCallback(MessageCallback cb);

    // 发布消息
    bool publish(const std::string& topic, const std::string& payload, int qos = 1);

    // 是否已连接
    bool isConnected() const { return connected_.load(); }

private:
    // 连接循环（断线自动重连）
    void connectLoop();

    // 订阅所有 ESP32 上行 topic
    void subscribeAll();

    // 内部消息处理
    void handleMessage(mqtt::const_message_ptr msg);

private:
    std::string         broker_url_;
    std::string         client_id_;
    mqtt::async_client* client_ = nullptr;
    std::atomic<bool>   connected_{false};
    std::atomic<bool>   running_{false};
    std::thread         thread_;
    MessageCallback     callback_;
    std::mutex          cb_mutex_;
};

#endif // MQTT_CLIENT_H
