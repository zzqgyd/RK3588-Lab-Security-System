#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include "devices/iot_device.h"
#include "ipc/db.h"
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <map>

// ================================================================
// DeviceManager：多设备管理器（DB 驱动）
// ----------------------------------------------------------------
// 职责：
//   1. 启动时从 device_plugs 表加载插座配置，建立连接
//   2. 启动时主动关闭所有插座（安全策略：默认断电）
//   3. 异步轮询设备状态（fast 5s）
//   4. 提供命令式 API（setPower / refreshOne / addPlug / delPlug 等）
//   5. 过载/过温自动断电保护
//   6. 设备事件统一回调
//   7. 响应 IPC 命令（QT 查询/控制，主进程登记/释放）
//
// 数据库：
//   复用主进程的 records.db，使用 device_plugs 表
// ================================================================

// 安全保护配置
struct SafetyConfig {
    bool   enable_overload_protect = true;   // 过载自动断电
    double overload_threshold_w    = 2000.0; // 过载阈值（W）
    bool   enable_overtemp_protect = true;   // 过温自动断电
    int    overtemp_threshold_c    = 75;     // 过温阈值（℃）
    int    cooldown_seconds        = 60;     // 断电后冷却时间（秒）
};

class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();

    // 禁拷贝
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    // ============================================================
    // 生命周期
    // ============================================================

    // 初始化：打开 DB，加载插座配置，建立连接，关闭所有插座
    bool init(const char* db_path);

    // 启动轮询线程
    bool start();

    // 停止轮询线程并断开所有设备
    void stop();

    // ============================================================
    // 设备管理（DB + 内存同步）
    // ============================================================

    // 新增插座配置（写入 DB + 建立连接）
    // 返回 0=成功，-1=失败
    int addPlug(int room_id, int device_id, const std::string& name,
                const std::string& ip, const std::string& token, int enabled = 1);

    // 删除插座配置（从 DB 删除 + 断开连接）
    bool removePlug(int room_id, int device_id);

    // 获取指定房间的所有插座状态（供 QT 查询）
    std::vector<nlohmann::json> getRoomStatus(int room_id);

    // 查询哪些房间有插座（供 QT 显示房间列表）
    std::vector<int> getRoomsWithPlugs();

    // ============================================================
    // 控制命令（供 IPC 调用）
    // ============================================================

    // 按 room_id + device_id 控制开关
    bool setPower(int room_id, int device_id, bool on);

    // 按 room_id + device_id 刷新状态
    bool refreshOne(int room_id, int device_id);

    // 设备登记成功：打开插座 + 启动倒计时
    bool onDeviceRegister(int room_id, int device_id, int duration_minutes);

    // 设备释放/提前结束：关闭插座 + 取消倒计时
    bool onDeviceRelease(int room_id, int device_id);

    // ============================================================
    // 配置与回调
    // ============================================================

    void setSafetyConfig(const SafetyConfig& cfg);
    void setEventCallback(DeviceEventCallback cb);

    // 获取 DB 句柄（供 Esp32Manager 复用）
    void* getDbHandle() { return db_; }

private:
    // 根据 room_id + device_id 查找设备
    std::shared_ptr<IotDevice> findDevice(int room_id, int device_id);

    // 根据 DB 配置创建设备实例
    std::shared_ptr<IotDevice> createDevice(const DevicePlugRow& row);

    // 轮询线程函数
    void fastPollLoop();

    // 安全保护检查
    void checkSafety(std::shared_ptr<IotDevice> device);

    // 关闭所有插座（安全策略）
    void powerOffAll();

    // 软件计时：检查到期设备并自动断电
    void checkCountdownExpiry();

    // 软件计时：获取指定设备的剩余分钟数（0=未计时）
    int getRemainingMinutes(int room_id, int device_id);

private:
    mutable std::mutex mutex_;
    void* db_ = nullptr;                                   // sqlite3* 句柄
    std::vector<std::shared_ptr<IotDevice>> devices_;

    // 轮询线程
    std::thread fast_poller_;
    std::atomic<bool> running_{false};

    // 配置
    SafetyConfig safety_cfg_;
    DeviceEventCallback event_cb_;

    // 安全保护：设备冷却时间记录（key=room_id*100+device_id → 冷却结束时间戳 ms）
    std::mutex safety_mutex_;
    std::map<int, int64_t> safety_cooldown_;

    // 软件计时：设备到期时间表（key=room_id*100+device_id → 到期时间戳 ms）
    // 登记时设置，到期后自动 RELEASE 并从表中移除
    std::mutex countdown_mutex_;
    std::map<int, int64_t> countdown_expire_;              // 到期时间戳（ms）
};

#endif // DEVICE_MANAGER_H
