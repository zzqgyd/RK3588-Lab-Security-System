#ifndef MIOT_PLUG_H
#define MIOT_PLUG_H

#include "devices/iot_device.h"
#include "miio/miio_client.h"
#include <mutex>

// ================================================================
// MiotPlug：米家智能插座3（cuco.plug.v3）实现
// ----------------------------------------------------------------
// 基于 MIOT Spec 协议，通过 siid/piid 访问设备属性。
//
// 本项目用到的 MIOT Spec 属性（来自官方 spec 表）：
//
//   Service #2 (switch 开关)
//     piid 1  on              bool    开关状态（R/W）
//     piid 2  default-power-on-state  uint8   默认上电状态（R/W）
//     piid 3  fault           uint8   故障码（R）
//
//   Service #11 (power-consumption 功耗参数)
//     piid 1  power-consumption  uint16  累计电量（0.01KWh）
//     piid 2  electric-power    float   瞬时功率（W）
//
//   Service #12 (on-off-count 开关次数)
//     piid 1  on-off-count    uint8   开关次数
//     piid 2  temperature     uint8   设备温度（℃）
//
//   Service #8 (quick-countdown 快捷倒计时)
//     piid 1  on              bool    倒计时开关
//     piid 2  duration        uint16  时长（分钟）
//     piid 3  left-time       uint16  剩余时间
//
// ================================================================

// 插座状态缓存（线程安全读取）
struct PlugStatus {
    bool        is_on;              // 开关状态
    int         default_power_on;   // 默认上电状态：0恢复/1开/2关
    int         fault;              // 故障码：0无/1过温/2过载
    double      power_w;            // 瞬时功率（W）
    uint16_t    energy_kwh_x100;    // 累计电量（0.01KWh 为单位）
    int         on_off_count;       // 开关次数
    int         temperature;        // 设备温度（℃）
    int         countdown_left;     // 倒计时剩余分钟（0=未启用）

    // 时间戳：上一次刷新成功的时间
    int64_t     last_update_ms;

    PlugStatus() : is_on(false), default_power_on(0), fault(0),
                   power_w(0), energy_kwh_x100(0), on_off_count(0),
                   temperature(0), countdown_left(0), last_update_ms(0) {}
};

class MiotPlug : public IotDevice {
public:
    // 构造：id(DB主键), name, room_id, device_id(房间内设备号), ip, port(默认54321), token(32位hex)
    MiotPlug(int id, const std::string& name, int room_id, int device_id,
             const std::string& ip, uint16_t port, const std::string& token);
    ~MiotPlug() override;

    // ====== 实现 IotDevice 接口 ======
    bool connect() override;
    void disconnect() override;
    bool refresh() override;
    nlohmann::json getStatus() const override;
    bool setPower(bool on) override;

    // ====== 米家插座特有方法 ======

    // 设置默认上电状态（0恢复/1开/2关）
    bool setDefaultPowerOn(int state);

    // 启动快捷倒计时关闭
    //   duration_minutes: 倒计时分钟数
    bool startCountdown(int duration_minutes);

    // 取消倒计时（关闭倒计时功能）
    bool cancelCountdown();

    // 获取完整状态缓存（线程安全）
    PlugStatus getPlugStatus() const;

    // 获取设备 ID（米家 did，握手后有效；未连接返回 0）
    uint32_t getDid() const {
        return client_ ? client_->getDid() : 0;
    }

    // MIOT Spec siid/piid 常量（公开，方便外部按需扩展查询）
    static constexpr int SIID_SWITCH        = 2;
    static constexpr int SIID_POWER_CONS    = 11;
    static constexpr int SIID_ON_OFF_COUNT  = 12;
    static constexpr int SIID_COUNTDOWN     = 8;

    static constexpr int PIID_ON            = 1;
    static constexpr int PIID_DEFAULT_ON    = 2;
    static constexpr int PIID_FAULT         = 3;
    static constexpr int PIID_ENERGY        = 1;   // 11.1
    static constexpr int PIID_POWER_W       = 2;   // 11.2
    static constexpr int PIID_COUNT         = 1;   // 12.1
    static constexpr int PIID_TEMP          = 2;   // 12.2
    static constexpr int PIID_CD_ON         = 1;   // 8.1
    static constexpr int PIID_CD_LEFT       = 3;   // 8.3

    // 故障码 → 文字描述
    static const char* faultDesc(int fault) {
        switch (fault) {
            case 1:  return "过温";
            case 2:  return "过载";
            case 0:  return "无故障";
            default: return "未知故障";
        }
    }

private:
    // 检查故障并触发事件（过温/过载）
    void checkFault(int old_fault, int new_fault);

    std::unique_ptr<miio::MiioClient> client_;
    mutable std::mutex status_mutex_;
    PlugStatus status_;
    std::string ip_;
    uint16_t   port_;
    std::string token_;
    bool       last_on_state_;   // 上一次开关状态（用于变化检测）
    int        consecutive_failures_;  // 连续失败计数（>=3 标记离线）

    // 自动重连相关
    bool       token_valid_;         // token 是否合法（构造 MiioClient 成功）
    int64_t    last_reconnect_ms_;   // 上次重连时间戳（限制重连频率）
};

#endif // MIOT_PLUG_H
