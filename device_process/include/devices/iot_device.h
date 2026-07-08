#ifndef IOT_DEVICE_H
#define IOT_DEVICE_H

#include <string>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>

// ================================================================
// IotDevice：物联网设备抽象基类
// ----------------------------------------------------------------
// 设计目的：
//   - 解耦"设备协议层"与"设备管理层"
//   - 未来新增设备类型（ESP32/其他米家设备）只需继承此类，
//     DeviceManager 无需修改（开闭原则）
//
// 抽象维度：
//   1. 连接管理：connect / disconnect / isConnected
//   2. 状态刷新：refresh（从设备拉取最新状态到本地缓存）
//   3. 状态查询：getStatus（返回缓存的状态 JSON，不阻塞）
//   4. 控制命令：setPower（开关）/ callAction（通用动作）
//   5. 事件回调：状态变化时通知上层（如过温告警）
// ================================================================

// 设备状态变化事件类型
// 注意：原名 DeviceEvent 与 ipc/ipc_socket.h 中的 C 结构体 DeviceEvent 冲突，
//       故改名为 DeviceEventType（仅 device_process 内部使用）。
enum class DeviceEventType {
    kOnline,            // 设备上线
    kOffline,           // 设备离线
    kPowerOn,           // 开关打开
    kPowerOff,          // 开关关闭
    kFaultOverTemp,     // 过温故障
    kFaultOverload,     // 过载故障
    kFaultCleared,      // 故障清除
    kStateChanged       // 通用状态变化
};

// 设备类型枚举（方便后续扩展）
enum class DeviceType {
    kMiioPlug,          // 米家插座（miio 协议）
    kEsp32Camera,       // ESP32 摄像头（未来扩展）
    kCustom             // 自定义设备
};

// 事件回调签名：参数为设备 ID 和事件类型
using DeviceEventCallback = std::function<void(int device_id, DeviceEventType event, const nlohmann::json& data)>;

class IotDevice {
public:
    IotDevice(int id, DeviceType type, const std::string& name, int room_id, int device_id)
        : id_(id), type_(type), name_(name), room_id_(room_id), device_id_(device_id),
          online_(false), event_cb_(nullptr) {}
    virtual ~IotDevice() = default;

    // 禁止拷贝
    IotDevice(const IotDevice&) = delete;
    IotDevice& operator=(const IotDevice&) = delete;

    // ============================================================
    // 基础信息
    // ============================================================
    int getId() const { return id_; }                  // DB 主键
    DeviceType getType() const { return type_; }
    std::string getName() const { return name_; }
    int getRoomId() const { return room_id_; }
    int getDeviceId() const { return device_id_; }     // 房间内设备号（对应主进程 ROI 设备号）
    bool isOnline() const { return online_; }

    // 设置事件回调
    void setEventCallback(DeviceEventCallback cb) {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        event_cb_ = cb;
    }

    // ============================================================
    // 子类必须实现的接口
    // ============================================================

    // 连接设备（子类实现具体协议握手）
    virtual bool connect() = 0;

    // 断开
    virtual void disconnect() = 0;

    // 拉取最新状态并缓存（由 DeviceManager 周期调用）
    // 返回 false 表示通信失败
    virtual bool refresh() = 0;

    // 返回缓存的状态 JSON（不阻塞，格式由子类定义）
    virtual nlohmann::json getStatus() const = 0;

    // 控制开关
    virtual bool setPower(bool on) = 0;

    // 通用动作调用（子类按需实现，默认空实现）
    virtual bool callAction(const std::string& action_name,
                           const nlohmann::json& params,
                           nlohmann::json& result) {
        (void)action_name; (void)params; (void)result;
        return false;
    }

protected:
    // 子类调用：通知上层事件
    void notifyEvent(DeviceEventType event, const nlohmann::json& data) {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        if (event_cb_) event_cb_(id_, event, data);
    }

    // 子类调用：更新在线状态
    void setOnline(bool online) {
        bool was_online = online_.exchange(online);
        if (was_online != online) {
            notifyEvent(online ? DeviceEventType::kOnline : DeviceEventType::kOffline,
                       {{"name", name_}});
        }
    }

    // 成员
    int         id_;          // 设备唯一 ID（DB 主键）
    DeviceType  type_;
    std::string name_;
    int         room_id_;
    int         device_id_;   // 房间内设备号（对应主进程 ROI 设备号）
    std::atomic<bool> online_;
    DeviceEventCallback event_cb_;
    mutable std::mutex cb_mutex_;
};

#endif // IOT_DEVICE_H
