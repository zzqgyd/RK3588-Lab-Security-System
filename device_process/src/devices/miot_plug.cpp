// ================================================================
// MiotPlug 实现：米家智能插座3 控制
// ================================================================

#include "devices/miot_plug.h"
#include <chrono>
#include <cstring>
#include <cstdio>

// 获取当前毫秒时间戳
static int64_t now_ms() {
    auto tp = std::chrono::system_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch());
    return dur.count();
}

MiotPlug::MiotPlug(int id, const std::string& name, int room_id, int device_id,
                   const std::string& ip, uint16_t port, const std::string& token)
    : IotDevice(id, DeviceType::kMiioPlug, name, room_id, device_id),
      ip_(ip), port_(port), token_(token),
      last_on_state_(false), consecutive_failures_(0),
      token_valid_(true), last_reconnect_ms_(0)
{
    // 延迟到 connect() 才创建 MiioClient
}

MiotPlug::~MiotPlug()
{
    disconnect();
}

// ================================================================
// connect：建立 miio 连接
// ================================================================
bool MiotPlug::connect()
{
    // 创建 MiioClient（如尚未创建）
    if (!client_) {
        try {
            client_ = std::make_unique<miio::MiioClient>(ip_, port_, token_);
            token_valid_ = true;
        } catch (const std::exception& e) {
            fprintf(stderr, "[MiotPlug %d] 创建 MiioClient 失败: %s\n", id_, e.what());
            token_valid_ = false;  // token 非法，不再重试创建
            setOnline(false);
            return false;
        }
    }

    bool ok = client_->connect();
    setOnline(ok);
    last_reconnect_ms_ = now_ms();
    if (ok) {
        // 连接成功后立刻拉取一次状态
        refresh();
    }
    return ok;
}

void MiotPlug::disconnect()
{
    // MiioClient 析构会关闭 socket
    client_.reset();
    setOnline(false);
}

// ================================================================
// refresh：批量拉取所有需要的属性
// ----------------------------------------------------------------
// 设计：单次 RPC 拉取 7 个属性，减少设备通信次数
//   2.1 开关 / 2.3 故障 / 11.2 功率 / 11.1 电量
//   12.1 开关次数 / 12.2 温度 / 8.3 倒计时剩余
// ================================================================
bool MiotPlug::refresh()
{
    // ====== 自动重连逻辑 ======
    // 1. token 非法（MiioClient 构造失败）：不重试，直接返回
    // 2. client_ 存在但未连接：每 10 秒重试一次 connect()
    // 3. client_ 为 null 但 token 合法：理论不会发生（connect() 会创建）
    if (!token_valid_) {
        setOnline(false);
        return false;
    }
    if (!client_ || !client_->isConnected()) {
        // 限制重连频率：10 秒一次
        int64_t now = now_ms();
        if (now - last_reconnect_ms_ < 10000) {
            setOnline(false);
            return false;
        }
        printf("[MiotPlug %d] 尝试重连 %s:%u ...\n", id_, ip_.c_str(), port_);
        if (!connect()) {
            setOnline(false);
            return false;
        }
    }

    // 批量查询属性列表
    std::vector<std::pair<int,int>> props = {
        {SIID_SWITCH,       PIID_ON},          // 2.1 开关
        {SIID_SWITCH,       PIID_FAULT},       // 2.3 故障
        {SIID_POWER_CONS,   PIID_POWER_W},     // 11.2 功率
        {SIID_POWER_CONS,   PIID_ENERGY},      // 11.1 电量
        {SIID_ON_OFF_COUNT, PIID_COUNT},       // 12.1 开关次数
        {SIID_ON_OFF_COUNT, PIID_TEMP},        // 12.2 温度
        {SIID_COUNTDOWN,    PIID_CD_LEFT}      // 8.3 倒计时剩余
    };

    nlohmann::json values;
    if (!client_->getProperties(props, values)) {
        // 单次失败不立即标记离线（可能是设备忙或丢包）
        // 仅在连续失败时才标记离线
        if (++consecutive_failures_ >= 3) {
            setOnline(false);
        }
        return false;
    }

    // 解析响应：result 是数组，每项含 siid/piid/value
    if (!values.is_array() || values.size() != props.size()) {
        if (++consecutive_failures_ >= 3) {
            setOnline(false);
        }
        return false;
    }

    consecutive_failures_ = 0;

    // 临时变量
    int old_fault = 0;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        old_fault = status_.fault;
    }

    PlugStatus new_status;
    // 默认填充上次值（避免某次失败丢失缓存）
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        new_status = status_;
    }

    // 按顺序解析（与 props 顺序一致）
    try {
        // 2.1 开关
        if (values[0].contains("value"))
            new_status.is_on = values[0]["value"].get<bool>();

        // 2.3 故障
        if (values[1].contains("value"))
            new_status.fault = values[1]["value"].get<int>();

        // 11.2 功率
        if (values[2].contains("value"))
            new_status.power_w = values[2]["value"].get<double>();

        // 11.1 电量
        if (values[3].contains("value"))
            new_status.energy_kwh_x100 = values[3]["value"].get<uint16_t>();

        // 12.1 开关次数
        if (values[4].contains("value"))
            new_status.on_off_count = values[4]["value"].get<int>();

        // 12.2 温度
        if (values[5].contains("value"))
            new_status.temperature = values[5]["value"].get<int>();

        // 8.3 倒计时剩余
        if (values[6].contains("value"))
            new_status.countdown_left = values[6]["value"].get<int>();
    } catch (const std::exception& e) {
        fprintf(stderr, "[MiotPlug %d] 解析状态失败: %s\n", id_, e.what());
        setOnline(false);
        return false;
    }

    new_status.last_update_ms = now_ms();

    // 更新缓存
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_ = new_status;
    }

    setOnline(true);

    // 触发状态变化事件
    notifyEvent(DeviceEventType::kStateChanged, getStatus());

    // 故障变化通知
    checkFault(old_fault, new_status.fault);

    // 开关状态变化通知
    if (new_status.is_on != last_on_state_) {
        notifyEvent(new_status.is_on ? DeviceEventType::kPowerOn : DeviceEventType::kPowerOff,
                   {{"name", name_}});
        last_on_state_ = new_status.is_on;
    }

    return true;
}

// ================================================================
// getStatus：返回缓存状态的 JSON 表示
// ================================================================
nlohmann::json MiotPlug::getStatus() const
{
    std::lock_guard<std::mutex> lock(status_mutex_);
    return {
        {"id", id_},
        {"name", name_},
        {"room_id", room_id_},
        {"device_id", device_id_},
        {"type", "miio_plug"},
        {"online", online_.load()},
        {"is_on", status_.is_on},
        {"default_power_on", status_.default_power_on},
        {"fault", status_.fault},
        {"fault_desc", faultDesc(status_.fault)},
        {"power_w", status_.power_w},
        {"energy_kwh", status_.energy_kwh_x100 / 100.0},
        {"on_off_count", status_.on_off_count},
        {"temperature", status_.temperature},
        {"countdown_left_min", status_.countdown_left},
        {"last_update_ms", status_.last_update_ms}
    };
}

// ================================================================
// setPower：控制开关
// ================================================================
bool MiotPlug::setPower(bool on)
{
    if (!client_ || !client_->isConnected()) return false;

    // set_properties: [{"siid":2,"piid":1,"value":true}]
    nlohmann::json props = nlohmann::json::array({
        {{"siid", SIID_SWITCH}, {"piid", PIID_ON}, {"value", on}}
    });

    if (!client_->setProperties(props)) return false;

    // 主动刷新一次状态（保证缓存及时）
    refresh();
    return true;
}

// ================================================================
// setDefaultPowerOn：设置默认上电状态
// ================================================================
bool MiotPlug::setDefaultPowerOn(int state)
{
    if (!client_ || !client_->isConnected()) return false;
    if (state < 0 || state > 2) return false;

    nlohmann::json props = nlohmann::json::array({
        {{"siid", SIID_SWITCH}, {"piid", PIID_DEFAULT_ON}, {"value", state}}
    });

    if (!client_->setProperties(props)) return false;

    // 同步更新缓存
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.default_power_on = state;
    }
    return true;
}

// ================================================================
// startCountdown：启动倒计时关闭
// ----------------------------------------------------------------
// MIOT Spec 写法：先设 duration，再开启 on=true
// ================================================================
bool MiotPlug::startCountdown(int duration_minutes)
{
    if (!client_ || !client_->isConnected()) return false;
    if (duration_minutes <= 0 || duration_minutes > 1440) return false;

    // 步骤1：设置时长
    nlohmann::json props = nlohmann::json::array({
        {{"siid", SIID_COUNTDOWN}, {"piid", 2}, {"value", duration_minutes}}
    });
    if (!client_->setProperties(props)) return false;

    // 步骤2：开启倒计时
    props = nlohmann::json::array({
        {{"siid", SIID_COUNTDOWN}, {"piid", PIID_CD_ON}, {"value", true}}
    });
    if (!client_->setProperties(props)) return false;

    return true;
}

// ================================================================
// cancelCountdown：取消倒计时
// ================================================================
bool MiotPlug::cancelCountdown()
{
    if (!client_ || !client_->isConnected()) return false;

    nlohmann::json props = nlohmann::json::array({
        {{"siid", SIID_COUNTDOWN}, {"piid", PIID_CD_ON}, {"value", false}}
    });
    return client_->setProperties(props);
}

// ================================================================
// getPlugStatus：返回状态缓存副本
// ================================================================
PlugStatus MiotPlug::getPlugStatus() const
{
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_;
}

// ================================================================
// checkFault：故障检测与事件通知
// ----------------------------------------------------------------
// fault 码：0 无故障 / 1 过温 / 2 过载
// ================================================================
void MiotPlug::checkFault(int old_fault, int new_fault)
{
    if (old_fault == new_fault) return;

    switch (new_fault) {
        case 1:
            notifyEvent(DeviceEventType::kFaultOverTemp,
                       {{"name", name_}, {"temperature", status_.temperature}});
            break;
        case 2:
            notifyEvent(DeviceEventType::kFaultOverload,
                       {{"name", name_}, {"power_w", status_.power_w}});
            break;
        case 0:
        default:
            notifyEvent(DeviceEventType::kFaultCleared, {{"name", name_}});
            break;
    }
}
