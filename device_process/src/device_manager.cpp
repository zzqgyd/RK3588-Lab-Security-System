// ================================================================
// DeviceManager 实现（DB 驱动）
// ================================================================

#include "device_manager.h"
#include "devices/miot_plug.h"
#include "ipc/db.h"
#include <chrono>
#include <cstdio>
#include <cstring>

// 获取当前毫秒时间戳
static int64_t now_ms() {
    auto tp = std::chrono::system_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch());
    return dur.count();
}

DeviceManager::DeviceManager() {}

DeviceManager::~DeviceManager()
{
    stop();
    if (db_) db_close(db_);
}

// ================================================================
// 生命周期
// ================================================================
bool DeviceManager::init(const char* db_path)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. 打开数据库（复用主进程的 records.db）
    db_ = db_open(db_path);
    if (!db_) {
        fprintf(stderr, "[DeviceManager] 打开 DB 失败: %s\n", db_path);
        return false;
    }
    printf("[DeviceManager] DB 已打开: %s\n", db_path);

    // 2. 加载所有已配置的插座
    DevicePlugRow rows[64];
    for (int room = 0; room < 8; ++room) {
        int n = db_query_plugs_by_room(db_, room, rows, 64);
        for (int i = 0; i < n; ++i) {
            if (!rows[i].enabled) continue;
            auto dev = createDevice(rows[i]);
            if (dev) {
                if (event_cb_) dev->setEventCallback(event_cb_);
                devices_.push_back(dev);
                printf("[DeviceManager] 加载插座: 房间%d 设备%d %s %s\n",
                       rows[i].room_id, rows[i].device_id, rows[i].name, rows[i].ip);
            }
        }
    }

    // 3. 异步连接所有设备（不阻塞 init）
    for (auto& dev : devices_) {
        if (dev) dev->connect();
    }

    // 4. 安全策略：关闭所有插座（默认断电）
    powerOffAll();

    return true;
}

bool DeviceManager::start()
{
    if (running_.exchange(true)) {
        return true;  // 已启动
    }

    // 启动 fast 轮询线程（5 秒一次状态刷新）
    fast_poller_ = std::thread([this] { fastPollLoop(); });
    return true;
}

void DeviceManager::stop()
{
    if (!running_.exchange(false)) {
        return;
    }

    if (fast_poller_.joinable()) fast_poller_.join();

    // 断开所有设备
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& dev : devices_) {
        if (dev) dev->disconnect();
    }
    devices_.clear();
}

// ================================================================
// 设备管理（DB + 内存同步）
// ================================================================

// 辅助：验证 token 是否是 32 位 hex 字符串
static bool isValidToken(const std::string& token)
{
    if (token.size() != 32) return false;
    for (char c : token) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

// 辅助：验证 IP 格式（简单检查）
static bool isValidIp(const std::string& ip)
{
    if (ip.empty() || ip.size() > 64) return false;
    // 简单检查：包含 . 且不包含空格
    if (ip.find('.') == std::string::npos) return false;
    if (ip.find(' ') != std::string::npos) return false;
    return true;
}

int DeviceManager::addPlug(int room_id, int device_id, const std::string& name,
                           const std::string& ip, const std::string& token, int enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return -1;

    // 0. 参数预验证（在写 DB 之前，避免垃圾数据入库）
    if (name.empty()) {
        fprintf(stderr, "[DeviceManager] 添加失败: 名称不能为空\n");
        return -1;
    }
    if (!isValidIp(ip)) {
        fprintf(stderr, "[DeviceManager] 添加失败: IP 格式错误 '%s'\n", ip.c_str());
        return -1;
    }
    if (!isValidToken(token)) {
        fprintf(stderr, "[DeviceManager] 添加失败: token 必须是 32 位 hex 字符串 (当前长度=%zu)\n",
                token.size());
        return -1;
    }

    // 1. 写入 DB
    if (db_insert_device_plug(db_, room_id, device_id,
                              name.c_str(), ip.c_str(), token.c_str(), enabled) != 0) {
        fprintf(stderr, "[DeviceManager] 写入 DB 失败\n");
        return -1;
    }

    // 2. 查回完整行（拿到自增 id）
    DevicePlugRow row;
    if (db_query_plug(db_, room_id, device_id, &row) != 0) {
        fprintf(stderr, "[DeviceManager] 写入后查询失败\n");
        // 回滚：删除刚插入的记录
        db_delete_device_plug(db_, room_id, device_id);
        return -1;
    }

    // 3. 创建设备并连接
    auto dev = createDevice(row);
    if (!dev) {
        fprintf(stderr, "[DeviceManager] 创建设备失败\n");
        db_delete_device_plug(db_, room_id, device_id);
        return -1;
    }
    if (event_cb_) dev->setEventCallback(event_cb_);

    // 4. 连接设备
    //    注意：connect() 失败不回滚 DB，因为设备可能暂时离线
    //    后续 fastPollLoop 会持续重试连接
    bool connected = dev->connect();
    if (!connected) {
        printf("[DeviceManager] 警告: 设备暂时无法连接（已加入 DB，将自动重试）: %s\n", ip.c_str());
    } else {
        // ★ 安全策略：连接成功后立即断电（默认 power-off）
        auto plug = std::dynamic_pointer_cast<MiotPlug>(dev);
        if (plug) {
            plug->cancelCountdown();
            plug->setPower(false);
            printf("[DeviceManager] 新插座已断电（安全策略）\n");
        }
    }

    devices_.push_back(dev);

    printf("[DeviceManager] 新增插座: 房间%d 设备%d %s %s (连接=%s)\n",
           room_id, device_id, name.c_str(), ip.c_str(),
           connected ? "成功" : "失败-离线");
    return 0;
}

bool DeviceManager::removePlug(int room_id, int device_id)
{
    // 1. 锁内：从 DB 删除 + 取出设备引用
    std::shared_ptr<IotDevice> dev;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return false;

        if (db_delete_device_plug(db_, room_id, device_id) != 0) {
            fprintf(stderr, "[DeviceManager] 从 DB 删除失败\n");
            return false;
        }

        for (auto it = devices_.begin(); it != devices_.end(); ++it) {
            if (*it && (*it)->getRoomId() == room_id && (*it)->getDeviceId() == device_id) {
                dev = *it;  // 保存引用，锁外断电
                devices_.erase(it);
                break;
            }
        }
    }

    // 2. 锁外：断电 + 断开连接（删除时插座可能正在使用，必须先断电）
    if (dev) {
        auto plug = std::dynamic_pointer_cast<MiotPlug>(dev);
        if (plug) {
            plug->setPower(false);  // 尝试断电，失败也无所谓（DB 已删）
        }
        dev->disconnect();
    }

    // 3. 清除软件计时（删除正在使用的插座时必须清计时）
    {
        int key = room_id * 100 + device_id;
        std::lock_guard<std::mutex> lk(countdown_mutex_);
        countdown_expire_.erase(key);
    }

    printf("[DeviceManager] 删除插座: 房间%d 设备%d\n", room_id, device_id);
    return true;
}

std::vector<nlohmann::json> DeviceManager::getRoomStatus(int room_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<nlohmann::json> result;
    for (auto& dev : devices_) {
        if (dev && dev->getRoomId() == room_id) {
            nlohmann::json s = dev->getStatus();
            // 用软件计时覆盖硬件倒计时（软件计时是权威来源）
            s["countdown_left_min"] = getRemainingMinutes(dev->getRoomId(), dev->getDeviceId());
            result.push_back(s);
        }
    }
    return result;
}

std::vector<int> DeviceManager::getRoomsWithPlugs()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> rooms;
    for (auto& dev : devices_) {
        if (!dev) continue;
        int r = dev->getRoomId();
        bool found = false;
        for (int x : rooms) {
            if (x == r) { found = true; break; }
        }
        if (!found) rooms.push_back(r);
    }
    return rooms;
}

// ================================================================
// 控制命令
// ================================================================
bool DeviceManager::setPower(int room_id, int device_id, bool on)
{
    // 锁内查找，锁外执行 RPC
    std::shared_ptr<IotDevice> dev;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dev = findDevice(room_id, device_id);
        if (!dev) {
            fprintf(stderr, "[DeviceManager] 未找到对应插座: 房间%d 设备%d\n", room_id, device_id);
            return false;
        }
    }
    return dev->setPower(on);
}

bool DeviceManager::refreshOne(int room_id, int device_id)
{
    std::shared_ptr<IotDevice> dev;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dev = findDevice(room_id, device_id);
    }
    return dev ? dev->refresh() : false;
}

bool DeviceManager::onDeviceRegister(int room_id, int device_id, int duration_minutes)
{
    // 1. 锁内查找设备（shared_ptr 保证 RPC 期间设备不被销毁）
    std::shared_ptr<IotDevice> dev;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dev = findDevice(room_id, device_id);
        if (!dev) {
            fprintf(stderr, "[DeviceManager] 未找到对应插座: 房间%d 设备%d\n", room_id, device_id);
            return false;
        }
    }

    // 2. 锁外做 RPC（避免长时间持锁阻塞 QT 查询）
    auto plug = std::dynamic_pointer_cast<MiotPlug>(dev);
    if (!plug) return false;

    // 打开插座
    if (!plug->setPower(true)) {
        fprintf(stderr, "[DeviceManager] 打开插座失败: 房间%d 设备%d\n", room_id, device_id);
        return false;
    }

    // 3. 启动软件倒计时（主进程自己计时，到点自动 RELEASE）
    //    不依赖插座硬件倒计时功能（部分设备不支持或不可靠）
    if (duration_minutes > 0 && duration_minutes <= 1440) {
        int key = room_id * 100 + device_id;
        int64_t expire_ms = now_ms() + (int64_t)duration_minutes * 60 * 1000;
        {
            std::lock_guard<std::mutex> lk(countdown_mutex_);
            countdown_expire_[key] = expire_ms;
        }
        printf("[DeviceManager] 软件计时启动: 房间%d 设备%d %d分钟后到期\n",
               room_id, device_id, duration_minutes);
    }

    printf("[DeviceManager] 设备登记: 房间%d 设备%d 时长%d分钟\n",
           room_id, device_id, duration_minutes);
    return true;
}

bool DeviceManager::onDeviceRelease(int room_id, int device_id)
{
    // 1. 锁内查找设备
    std::shared_ptr<IotDevice> dev;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dev = findDevice(room_id, device_id);
    }

    // 2. 锁外做 RPC（设备可能离线/已删除，此时只清计时不断电）
    bool ok = false;
    if (dev) {
        auto plug = std::dynamic_pointer_cast<MiotPlug>(dev);
        if (plug) {
            ok = plug->setPower(false);
        }
    } else {
        fprintf(stderr, "[DeviceManager] 未找到对应插座: 房间%d 设备%d（仍清除计时）\n",
                room_id, device_id);
    }

    // 3. 清除软件计时（无论设备是否存在都必须清，否则 checkCountdownExpiry 会反复尝试）
    {
        int key = room_id * 100 + device_id;
        std::lock_guard<std::mutex> lk(countdown_mutex_);
        countdown_expire_.erase(key);
    }

    printf("[DeviceManager] 设备释放: 房间%d 设备%d -> %s\n", room_id, device_id, ok ? "OK" : "FAIL");
    return ok;
}

// ================================================================
// 软件计时：检查到期设备并自动断电
// ----------------------------------------------------------------
// 由 fastPollLoop 每秒调用，检查所有登记的设备是否到期
// 到期则自动 onDeviceRelease（关闭插座 + 清除计时）
// ================================================================
void DeviceManager::checkCountdownExpiry()
{
    // 收集到期设备（锁内快速收集，锁外执行 RPC）
    std::vector<std::pair<int, int>> expired;  // (room_id, device_id)

    {
        std::lock_guard<std::mutex> lk(countdown_mutex_);
        if (countdown_expire_.empty()) return;

        int64_t now = now_ms();
        for (auto it = countdown_expire_.begin(); it != countdown_expire_.end(); ++it) {
            if (now >= it->second) {
                int key = it->first;
                expired.push_back({key / 100, key % 100});
            }
        }
    }

    // 锁外执行 RELEASE（避免阻塞 fastPollLoop）
    for (auto& p : expired) {
        printf("[DeviceManager] 软件计时到期，自动断电: 房间%d 设备%d\n",
               p.first, p.second);
        onDeviceRelease(p.first, p.second);
    }
}

// ================================================================
// 软件计时：获取指定设备的剩余分钟数
// ================================================================
int DeviceManager::getRemainingMinutes(int room_id, int device_id)
{
    int key = room_id * 100 + device_id;
    std::lock_guard<std::mutex> lk(countdown_mutex_);
    auto it = countdown_expire_.find(key);
    if (it == countdown_expire_.end()) return 0;

    int64_t remain_ms = it->second - now_ms();
    if (remain_ms <= 0) return 0;
    return (int)((remain_ms + 59999) / 60000);  // 向上取整到分钟
}

// ================================================================
// 配置与回调
// ================================================================
void DeviceManager::setSafetyConfig(const SafetyConfig& cfg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    safety_cfg_ = cfg;
}

void DeviceManager::setEventCallback(DeviceEventCallback cb)
{
    std::lock_guard<std::mutex> lock(mutex_);
    event_cb_ = cb;
    for (auto& dev : devices_) {
        if (dev) dev->setEventCallback(cb);
    }
}

// ================================================================
// 私有方法
// ================================================================
std::shared_ptr<IotDevice> DeviceManager::findDevice(int room_id, int device_id)
{
    for (auto& dev : devices_) {
        if (dev && dev->getRoomId() == room_id && dev->getDeviceId() == device_id) {
            return dev;
        }
    }
    return nullptr;
}

std::shared_ptr<IotDevice> DeviceManager::createDevice(const DevicePlugRow& row)
{
    if (true) {  // 目前只支持米家插座
        return std::make_shared<MiotPlug>(row.id, row.name, row.room_id, row.device_id,
                                          row.ip, 54321, row.token);
    }
    // 未来扩展：ESP32 等
    return nullptr;
}

// ================================================================
// fastPollLoop：5 秒轮询设备状态 + 1 秒检查软件计时到期
// ----------------------------------------------------------------
// 设备状态刷新：5 秒一次（避免过于频繁）
// 软件计时检查：1 秒一次（保证到期及时断电）
// ================================================================
void DeviceManager::fastPollLoop()
{
    int tick = 0;  // 计数器：每 5 个 tick 做一次 refresh
    while (running_.load()) {
        // 1. 每秒检查软件计时是否到期
        checkCountdownExpiry();

        // 2. 每 5 秒刷新一次设备状态
        if (tick == 0) {
            std::vector<std::shared_ptr<IotDevice>> snapshot;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                snapshot = devices_;
            }

            for (auto& dev : snapshot) {
                if (!running_.load()) break;
                if (!dev) continue;
                dev->refresh();
                checkSafety(dev);
            }
        }
        tick = (tick + 1) % 5;

        // 1 秒间隔
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ================================================================
// checkSafety：过载/过温自动断电保护
// ================================================================
void DeviceManager::checkSafety(std::shared_ptr<IotDevice> device)
{
    if (!device) return;

    auto plug = std::dynamic_pointer_cast<MiotPlug>(device);
    if (!plug) return;

    PlugStatus s = plug->getPlugStatus();
    int key = device->getRoomId() * 100 + device->getDeviceId();

    bool need_cutoff = false;
    std::string reason;

    if (safety_cfg_.enable_overtemp_protect) {
        if (s.fault == 1 || s.temperature > safety_cfg_.overtemp_threshold_c) {
            need_cutoff = true;
            reason = "过温";
        }
    }

    if (safety_cfg_.enable_overload_protect) {
        if (s.fault == 2 || s.power_w > safety_cfg_.overload_threshold_w) {
            need_cutoff = true;
            reason = "过载";
        }
    }

    if (!need_cutoff) return;

    // 检查是否已在冷却期
    {
        std::lock_guard<std::mutex> lock(safety_mutex_);
        auto it = safety_cooldown_.find(key);
        if (it != safety_cooldown_.end() && now_ms() < it->second) {
            return;  // 已在冷却期，不再重复触发
        }
    }

    // 触发断电
    fprintf(stderr, "[Safety] 房间%d 设备%d 触发 %s 保护，自动断电\n",
            device->getRoomId(), device->getDeviceId(), reason.c_str());
    device->setPower(false);

    // 设置冷却期
    {
        std::lock_guard<std::mutex> lock(safety_mutex_);
        safety_cooldown_[key] = now_ms() + safety_cfg_.cooldown_seconds * 1000LL;
    }

    // 通知事件
    if (event_cb_) {
        nlohmann::json data = {
            {"room_id", device->getRoomId()},
            {"device_id", device->getDeviceId()},
            {"reason", reason},
            {"temperature", s.temperature},
            {"power_w", s.power_w},
            {"fault", s.fault}
        };
        if (reason == "过温") {
            event_cb_(device->getId(), DeviceEventType::kFaultOverTemp, data);
        } else {
            event_cb_(device->getId(), DeviceEventType::kFaultOverload, data);
        }
    }
}

// ================================================================
// powerOffAll：关闭所有插座（启动时调用）
// ================================================================
void DeviceManager::powerOffAll()
{
    printf("[DeviceManager] 安全策略：关闭所有插座\n");
    for (auto& dev : devices_) {
        if (!dev) continue;
        auto plug = std::dynamic_pointer_cast<MiotPlug>(dev);
        if (plug) {
            plug->cancelCountdown();
            plug->setPower(false);
        }
    }
}
