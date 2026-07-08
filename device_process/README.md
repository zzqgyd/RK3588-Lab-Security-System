# device_service 开发文档

## 1. 项目概述

`device_service` 是一个独立的物联网设备管理服务进程，用于控制米家智能插座（cuco.plug.v3）。通过自实现的 miio 协议（C++）直接与设备通信，无需依赖 Python 或米家云端。

### 1.1 核心特性

- **纯 C++ 自实现 miio 协议**：UDP 54321 + AES-128-CBC + JSON-RPC，无 Python 依赖
- **多设备管理**：支持同时管理多个插座，按房间分组（内存管理，无持久化）
- **完整 spec 覆盖**：支持 cuco.plug.v3 所有 28 个 Properties 读写
- **安全保护**：过载/过温自动断电，可配置阈值与冷却时间
- **解耦架构**：抽象基类 `IotDevice` 支持未来扩展 ESP32 等其他设备
- **线程安全**：所有接口内部加锁，支持多线程并发调用

---

## 2. 架构设计

### 2.1 分层架构

```
┌─────────────────────────────────────────────────────────┐
│  应用层（main.cpp）                                      │
│    - 交互式 CLI / --test 单设备 Properties 测试          │
└───────────────────────────┬─────────────────────────────┘
                            │
┌───────────────────────────┴─────────────────────────────┐
│  管理层（DeviceManager）                                 │
│    - 多设备管理（内存增删/查询）                          │
│    - 分级轮询（fast 5s / slow 60s）                      │
│    - 过载/过温自动断电保护                                │
│    - 事件统一回调                                         │
└───────────────────────────┬─────────────────────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        │                   │                   │
┌───────┴───────┐   ┌───────┴───────┐   ┌──────┴────────┐
│ 业务层         │   │ ...（未来扩展）│   │               │
│ (MiotPlug)    │   │ (Esp32Device) │   │               │
│ - MIOT Spec   │   └───────────────┘   │               │
│   siid/piid   │                       │               │
│ - 状态缓存    │                       │               │
│ - 故障检测    │                       │               │
└───────┬───────┘                       │               │
        │                               │               │
┌───────┴───────────────────────────────────────────────┐
│  协议层（MiioClient）                                  │
│    - UDP 54321 收发                                    │
│    - Hello 握手（32B 包，0xFF 填充）                    │
│    - AES-128-CBC 加解密（key=md5(token), iv=md5(key+token)）│
│    - MD5 校验（md5(header+token+ciphertext)）          │
│    - JSON-RPC（get_properties/set_properties/action）  │
│    - RPC 重试（-9999 user ack timeout 自动重试 3 次）  │
└───────────────────────────────────────────────────────┘
        │
┌───────┴───────────────────────────────────────────────┐
│  抽象层（IotDevice）                                   │
│    - connect / disconnect / refresh                    │
│    - getStatus / setPower                              │
│    - 事件回调机制                                       │
└───────────────────────────────────────────────────────┘
```

### 2.2 设计原则

| 原则 | 实现方式 |
|------|---------|
| **开闭原则** | `IotDevice` 抽象基类，新增设备类型只需继承，`DeviceManager` 无需修改 |
| **单一职责** | 协议层/业务层/管理层各自独立，互不耦合 |
| **依赖倒置** | `DeviceManager` 依赖 `IotDevice` 抽象，不依赖具体 `MiotPlug` |
| **线程安全** | 所有公共接口内部加锁，`mutable` 成员支持 const 方法加锁 |
| **RAII** | `MiioClient` 析构关闭 socket，`MiotPlug` 析构断开连接 |
| **禁拷贝** | 所有管理类删除拷贝构造和赋值运算符 |

---

## 3. 目录结构

```
device_process/
├── CMakeLists.txt              # 构建配置
├── README.md                   # 本文档
│
├── include/                    # 头文件
│   ├── devices/
│   │   ├── iot_device.h        # 抽象基类 IotDevice
│   │   └── miot_plug.h         # 米家插座实现 MiotPlug
│   ├── miio/
│   │   └── miio_client.h       # miio 协议客户端
│   └── device_manager.h        # 多设备管理器
│
└── src/                        # 源文件
    ├── devices/
    │   └── miot_plug.cpp
    ├── miio/
    │   └── miio_client.cpp
    ├── device_manager.cpp
    └── main.cpp                # 入口（CLI + --test）
```

---

## 4. 模块详解

### 4.1 IotDevice（抽象基类）

**文件**：[include/devices/iot_device.h](include/devices/iot_device.h)

定义所有 IoT 设备的统一接口，支持未来扩展 ESP32 等设备。

```cpp
class IotDevice {
public:
    // 基础信息
    int getId() const;
    DeviceType getType() const;
    std::string getName() const;
    int getRoomId() const;
    bool isOnline() const;

    // 事件回调
    void setEventCallback(DeviceEventCallback cb);

    // 子类必须实现的接口
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool refresh() = 0;                    // 拉取最新状态到缓存
    virtual nlohmann::json getStatus() const = 0;  // 返回缓存 JSON（不阻塞）
    virtual bool setPower(bool on) = 0;

protected:
    void notifyEvent(DeviceEvent event, const nlohmann::json& data);
    void setOnline(bool online);
};
```

**设备事件类型**：
```cpp
enum class DeviceEvent {
    kOnline,            // 设备上线
    kOffline,           // 设备离线
    kPowerOn,           // 开关打开
    kPowerOff,          // 开关关闭
    kFaultOverTemp,     // 过温故障
    kFaultOverload,     // 过载故障
    kFaultCleared,      // 故障清除
    kStateChanged       // 通用状态变化
};
```

### 4.2 MiioClient（协议层）

**文件**：[include/miio/miio_client.h](include/miio/miio_client.h)、[src/miio/miio_client.cpp](src/miio/miio_client.cpp)

自实现的 miio 协议客户端，严格遵循 python-miio 的 protocol.py。

#### 4.2.1 miio 协议包结构

```
偏移  长度  字段          说明
0     2     magic        0x2131（大端）
2     2     length       总长度（含头 32B，大端）
4     4     unknown      固定 0x00000000
8     4     device_id    设备 ID（Hello 填 0，RPC 填握手得到的 did）
12    4     ts           设备 uptime（秒，从 Hello 响应获取，非 Unix 时间戳！）
16    16    checksum     MD5 校验（Hello 全 0；RPC = md5(header+token+ciphertext)）
32    N     payload      AES-128-CBC 加密的 JSON
```

#### 4.2.2 关键实现细节

| 细节 | 说明 |
|------|------|
| **Hello 包** | 32 字节，offset 0-3 为 `21 31 00 20`，offset 4-31 全部 `0xFF` |
| **ts 字段** | **设备 uptime（秒）**，非 Unix 时间戳！从 Hello 响应 offset 12-15 读取，每次 RPC 递增 1。设备会校验 ts 接近自身时钟，差距太大会直接丢包 |
| **AES 密钥派生** | `key = md5(token)`，`iv = md5(key + token)` |
| **加密** | 明文 = JSON + `\x00` 字节，AES-128-CBC + PKCS7 |
| **解密** | AES-128-CBC 解密后 `rstrip("\x00")` |
| **checksum** | `md5(header_16B + token_16B + ciphertext)`，注意顺序是 header + token + data |
| **-9999 错误** | `user ack timeout`，设备硬件响应慢，自动重试 3 次，间隔 500ms |
| **socket 清理** | 每次发送前清空接收缓冲区，避免收到上一个请求的延迟响应 |

#### 4.2.3 公开接口

```cpp
class MiioClient {
public:
    MiioClient(const std::string& ip, uint16_t port, const std::string& token_hex);

    bool connect();  // Hello 握手
    bool isConnected() const;

    // 通用 RPC（底层）
    bool rpcCall(const std::string& method,
                 const nlohmann::json& params,
                 nlohmann::json& response,
                 int timeout_ms = 3000);

    // MIOT Spec 高层封装
    bool getProperties(const std::vector<std::pair<int,int>>& props,
                      nlohmann::json& values,
                      int timeout_ms = 3000);

    bool setProperties(const nlohmann::json& props,
                      int timeout_ms = 3000);

    bool callAction(int siid, int aiid,
                    const nlohmann::json& in_params,
                    nlohmann::json& out_params,
                    int timeout_ms = 3000);

    uint32_t getDid() const;
};
```

### 4.3 MiotPlug（业务层）

**文件**：[include/devices/miot_plug.h](include/devices/miot_plug.h)、[src/devices/miot_plug.cpp](src/devices/miot_plug.cpp)

cuco.plug.v3 的 MIOT Spec 实现，维护设备状态缓存。

#### 4.3.1 MIOT Spec 属性映射

| Service | siid | Property | piid | 格式 | 读写 | 说明 |
|---------|------|----------|------|------|------|------|
| switch | 2 | on | 1 | bool | R/W | 开关状态 |
| switch | 2 | default-power-on-state | 2 | uint8 | R/W | 默认上电：0恢复/1开/2关 |
| switch | 2 | fault | 3 | uint8 | R | 故障码：0无/1过温/2过载 |
| physical-controls-locked | 7 | physical-controls-locked | 1 | bool | R/W | 物理控制锁 |
| power-consumption | 11 | power-consumption | 1 | uint16 | R | 累计电量（0.01KWh） |
| power-consumption | 11 | electric-power | 2 | float | R | 瞬时功率（W） |
| indicator-light | 13 | on | 1 | bool | R/W | 指示灯开关 |
| indicator-light | 3 | mode | 2 | bool | R/W | 勿扰模式 |
| indicator-light | 3 | start-time | 3 | uint16 | R/W | 勿扰开始时间（0~1440） |
| indicator-light | 3 | end-time | 4 | uint16 | R/W | 勿扰结束时间（0~1440） |
| charging-protection | 4 | on | 1 | bool | R/W | 充电保护开关 |
| charging-protection | 4 | power | 2 | uint8 | R/W | 功率阈值（2~10） |
| charging-protection | 4 | protect-time | 3 | uint8 | R/W | 保护时长（1~10） |
| cycle | 5 | status | 1 | bool | R/W | 循环任务状态 |
| cycle | 5 | data-value | 2 | string | R/W | 循环任务数据 |
| quick-countdown | 8 | on | 1 | bool | R/W | 倒计时开关 |
| quick-countdown | 8 | duration | 2 | uint16 | R/W | 倒计时时长（0~1440 分钟） |
| quick-countdown | 8 | left-time | 3 | uint16 | R/W | 倒计时剩余（0~1440 分钟） |
| max-power-limit | 9 | on | 1 | bool | R/W | 最大功率开关 |
| max-power-limit | 9 | power | 2 | uint16 | R/W | 最大功率阈值（1500~2500W） |
| over-use-ele-alert | 10 | on | 1 | bool | R/W | 过用电提醒开关 |
| over-use-ele-alert | 10 | over-ele-day | 2 | uint16 | R/W | 日阈值（1~60） |
| over-use-ele-alert | 10 | over-ele-month | 3 | uint16 | R/W | 月阈值（20~1800） |
| on-off-count | 12 | on-off-count | 1 | uint8 | R | 开关次数（0~255） |
| on-off-count | 12 | temperature | 2 | uint8 | R | 设备温度（0~150℃） |
| charge-prt-ext | 14 | power | 1 | uint16 | R/W | 充电保护功率阈值（2~600） |
| charge-prt-ext | 14 | protect-time | 2 | uint16 | R/W | 充电保护时长（1~300） |
| power-limit-ext | 15 | power-ext | 1 | uint16 | R/W | 功率限制阈值（300~2500W） |

#### 4.3.2 状态缓存结构

```cpp
struct PlugStatus {
    bool        is_on;              // 开关状态
    int         default_power_on;   // 默认上电状态
    int         fault;              // 故障码
    double      power_w;            // 瞬时功率（W）
    uint16_t    energy_kwh_x100;    // 累计电量（0.01KWh）
    int         on_off_count;       // 开关次数
    int         temperature;        // 设备温度（℃）
    int         countdown_left;     // 倒计时剩余分钟
    int64_t     last_update_ms;     // 最后刷新时间戳
};
```

#### 4.3.3 业务方法

```cpp
class MiotPlug : public IotDevice {
public:
    // IotDevice 接口实现
    bool connect() override;
    void disconnect() override;
    bool refresh() override;                    // 批量拉取 7 个核心属性
    nlohmann::json getStatus() const override;  // 返回 JSON 状态
    bool setPower(bool on) override;            // 控制开关

    // 米家插座特有
    bool setDefaultPowerOn(int state);          // 设置默认上电状态
    bool startCountdown(int duration_minutes);  // 启动倒计时关闭
    bool cancelCountdown();                     // 取消倒计时
    PlugStatus getPlugStatus() const;           // 获取完整状态缓存
    uint32_t getDid() const;                    // 获取米家 did
};
```

#### 4.3.4 refresh 批量查询

`refresh()` 单次 RPC 拉取 7 个核心属性，减少设备通信次数：

```cpp
std::vector<std::pair<int,int>> props = {
    {2, 1},    // 开关
    {2, 3},    // 故障
    {11, 2},   // 功率
    {11, 1},   // 电量
    {12, 1},   // 开关次数
    {12, 2},   // 温度
    {8, 3}     // 倒计时剩余
};
```

### 4.4 DeviceManager（管理层）

**文件**：[include/device_manager.h](include/device_manager.h)、[src/device_manager.cpp](src/device_manager.cpp)

#### 4.4.1 职责

1. 维护设备列表（**内存管理，无持久化**）
2. 启动分级轮询线程（fast 5s / slow 60s）
3. 提供命令式 API（setPower / refreshOne 等）
4. 过载/过温自动断电保护
5. 设备事件统一回调

#### 4.4.2 线程模型

```
主线程：调用 API（setPower / getStatus 等）
└── fast_poller_：每 5 秒轮询所有设备 refresh()
```

每个设备的 `refresh()` 内部自带 mutex，互不阻塞。

#### 4.4.3 安全保护

```cpp
struct SafetyConfig {
    bool   enable_overload_protect = true;   // 过载自动断电
    double overload_threshold_w    = 2000.0; // 过载阈值（W）
    bool   enable_overtemp_protect = true;   // 过温自动断电
    int    overtemp_threshold_c    = 75;     // 过温阈值（℃）
    int    cooldown_seconds        = 60;     // 断电后冷却时间（秒）
};
```

触发过载（fault=2 或 power > 阈值）或过温（fault=1 或 temp > 阈值）时：
1. 自动 `setPower(false)`
2. 进入冷却期（默认 60 秒），期间不再重复触发

#### 4.4.4 公开接口

```cpp
class DeviceManager {
public:
    DeviceManager();
    ~DeviceManager();

    // 生命周期
    bool start();  // 启动轮询
    void stop();

    // 设备管理（内存）
    int addDevice(const DeviceConfig& cfg);            // 返回 id
    bool removeDevice(int device_id);
    std::vector<nlohmann::json> getAllStatus();
    nlohmann::json getDeviceStatus(int device_id);
    std::vector<nlohmann::json> getRoomStatus(int room_id);

    // 控制命令
    bool setPower(int device_id, bool on);
    bool refreshOne(int device_id);
    bool setDefaultPowerOn(int device_id, int state);
    bool startCountdown(int device_id, int minutes);
    bool cancelCountdown(int device_id);

    // 配置
    void setSafetyConfig(const SafetyConfig& cfg);
    void setEventCallback(DeviceEventCallback cb);
};
```

---

## 5. 编译与运行

### 5.1 依赖

| 依赖 | 版本 | 用途 |
|------|------|------|
| OpenSSL | >= 1.1 | AES-128-CBC 加解密 + MD5 |
| nlohmann_json | >= 3.2 | JSON 解析 |

Ubuntu 安装：
```bash
sudo apt install libssl-dev nlohmann-json3-dev
```

### 5.2 编译

```bash
cd device_process
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 5.3 运行

#### 5.3.1 单设备 Properties 测试模式

```bash
./device_service --test <ip> <token>
# 例：
./device_service --test 192.168.184.235 9f68688b2d82b0c47dea2993c31c3e7e
```

按 cuco.plug.v3 spec 表完整测试所有 28 个 Properties：
- R/W 属性：读 → 写(测试值) → 读 → 恢复 → 读，确保状态不变
- R 属性：只读
- 测试完毕后设备状态恢复原值

#### 5.3.2 交互模式

```bash
./device_service
```

命令列表：
```
add                   添加插排
del <id>              删除插排
list                  列出所有插排
on <id>               打开插排
off <id>              关闭插排
status [id]           查询状态
room <room_id>        按房间查询
refresh <id>          刷新单个设备
countdown <id> <min>  启动倒计时关闭
cancel <id>           取消倒计时
default <id> <0/1/2>  设置默认上电状态
quit                  退出
```

注：交互模式为内存管理，进程退出后设备列表不保留。

---

## 6. 扩展指南

### 6.1 新增设备类型（如 ESP32）

1. **继承 `IotDevice`**：

```cpp
// include/devices/esp32_device.h
class Esp32Device : public IotDevice {
public:
    bool connect() override;
    void disconnect() override;
    bool refresh() override;
    nlohmann::json getStatus() const override;
    bool setPower(bool on) override;
    // ... ESP32 特有方法
};
```

2. **在 `DeviceManager::createDevice` 添加分支**：

```cpp
std::shared_ptr<IotDevice> DeviceManager::createDevice(const DeviceConfig& cfg)
{
    if (cfg.model == "cuco.plug.v3") {
        return std::make_shared<MiotPlug>(...);
    } else if (cfg.model == "esp32.cam") {
        return std::make_shared<Esp32Device>(...);
    }
    return nullptr;
}
```

3. **无需修改 `DeviceManager` 其他代码**（开闭原则）。

### 6.2 新增 MIOT Spec 属性查询

在 `MiotPlug::refresh()` 的批量查询列表中添加新属性：

```cpp
std::vector<std::pair<int,int>> props = {
    // ... 现有属性
    {15, 1},    // 新增：功率限制扩展
};
```

并在解析逻辑中添加对应字段。

---

## 7. 已验证功能清单

### 7.1 协议层（MiioClient）

- ✅ Hello 握手（32B 包，0xFF 填充）
- ✅ RPC ts 字段使用设备 uptime（非 Unix 时间戳）
- ✅ AES-128-CBC 加解密
- ✅ MD5 checksum
- ✅ get_properties / set_properties / miIO.info
- ✅ -9999 user ack timeout 自动重试
- ✅ socket 缓冲区清理

### 7.2 业务层（MiotPlug）

所有 28 个 Properties 已验证通过（`code:0`）：

| 服务 | 属性数 | 读写 | 验证结果 |
|------|--------|------|---------|
| #2 switch | 3 | R/W + R | ✅ |
| #7 physical-controls-locked | 1 | R/W | ✅ |
| #11 power-consumption | 2 | R | ✅ |
| #13 indicator-light | 1 | R/W | ✅ |
| #3 indicator-light 勿扰 | 3 | R/W | ✅ |
| #4 charging-protection | 3 | R/W | ✅ |
| #5 cycle | 2 | R/W | ✅ |
| #8 quick-countdown | 3 | R/W | ✅ |
| #9 max-power-limit | 2 | R/W | ✅ |
| #10 over-use-ele-alert | 3 | R/W | ✅ |
| #12 on-off-count | 2 | R | ✅ |
| #14 charge-prt-ext | 2 | R/W | ✅ |
| #15 power-limit-ext | 1 | R/W | ✅ |

### 7.3 管理层（DeviceManager）

- ✅ 多设备加载与异步连接
- ✅ 分级轮询（fast 5s）
- ✅ 过载/过温自动断电保护
- ✅ 事件回调机制
- ✅ 设备离线检测（连续 3 次失败才标记）

---

## 8. 已知限制

| 限制 | 说明 | 解决方案 |
|------|------|---------|
| **miio 事件推送不可用** | 事件推送需通过米家云端 MQTT 通道，本地 LAN 不接收 | 如需事件，需集成米家云端 SDK |
| **toggle action 不稳定** | cuco.plug.v3 的 toggle (siid=2,aiid=1) 固件实现存在 ack 超时（4+秒响应，返回 -9999） | 使用 `set_properties` 控制开关，稳定可靠 |
| **无持久化** | 设备列表仅在内存，进程退出后丢失 | 如需持久化，可在外部维护配置文件或集成 DB |
| **单进程** | 当前 `device_service` 是独立进程，与 main_process 无 IPC | 如需联动，可添加 IPC 通道 |

---

## 9. 参考资源

- [python-miio protocol.py](https://github.com/rytilahti/python-miio/blob/master/miio/protocol.py) - 协议参考实现
- [MIOT Spec](https://miot-spec.org/miot-spec-v2/instances?status=all) - 设备规范
- [OpenSSL EVP 文档](https://www.openssl.org/docs/manmaster/man3/EVP_EncryptInit.html) - AES API
