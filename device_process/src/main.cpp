// ================================================================
// device_service 入口（DB 驱动守护进程 + IPC 服务端）
// ----------------------------------------------------------------
// 默认模式：device_process 守护进程
//   1. 打开 records.db，加载 device_plugs 表中所有插座
//   2. 连接所有插座，并主动关闭所有插座（安全策略：默认断电）
//   3. 启动 5 秒轮询线程，刷新所有插座状态
//   4. 启动两个 IPC 服务端：
//        SOCK_PATH_QT_DEVICE   —— QT 查询/管理
//        SOCK_PATH_MAIN_DEVICE —— 主进程登记/释放事件
//   5. 等待 SIGINT/SIGTERM 退出
//
// 调试模式：./device_service --test <ip> <token>
//   完整测试 cuco.plug.v3 所有 28 个 Properties
//   R/W 属性做"读→改→读→恢复→读"，确保状态不变
// ================================================================

#include "device_manager.h"
#include "device_ipc_client.h"
#include "esp32_manager.h"
#include "devices/miot_plug.h"
#include "miio/miio_client.h"
#include "ipc/ipc_socket.h"
#include "ipc/db.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <unistd.h>

// DB 路径：复用主进程的 records.db
// 相对路径基于运行目录（install/ 下执行）
#define DB_PATH  "../../config/records.db"

// ================================================================
// 全局退出标志
// ================================================================
static std::atomic<bool> g_exit_flag{false};

static void onSignal(int sig)
{
    (void)sig;
    g_exit_flag.store(true);
}

// ================================================================
// 守护进程模式
// ================================================================
static int runDaemon()
{
    // 1. 注册信号
    signal(SIGINT,  onSignal);
    signal(SIGTERM, onSignal);
    signal(SIGPIPE, SIG_IGN);   // 写已关闭的 socket 不杀进程（返回 EPIPE）

    // 2. 初始化 DeviceManager（DB 驱动）
    DeviceManager mgr;

    mgr.setEventCallback([](int id, DeviceEventType event, const nlohmann::json& data) {
        const char* evt = "未知";
        switch (event) {
            case DeviceEventType::kOnline:        evt = "上线";       break;
            case DeviceEventType::kOffline:       evt = "离线";       break;
            case DeviceEventType::kPowerOn:       evt = "打开";       break;
            case DeviceEventType::kPowerOff:      evt = "关闭";       break;
            case DeviceEventType::kFaultOverTemp: evt = "过温告警";   break;
            case DeviceEventType::kFaultOverload: evt = "过载告警";   break;
            case DeviceEventType::kFaultCleared:  evt = "故障清除";   break;
            case DeviceEventType::kStateChanged:  evt = "状态变化";   break;
        }
        // 状态变化事件频率太高，只在故障/上下线时打印
        if (event != DeviceEventType::kStateChanged) {
            printf("[事件] 设备%d %s: %s\n", id, evt, data.dump().c_str());
        }
    });

    if (!mgr.init(DB_PATH)) {
        fprintf(stderr, "[main] DeviceManager 初始化失败\n");
        return 1;
    }

    if (!mgr.start()) {
        fprintf(stderr, "[main] 轮询线程启动失败\n");
        return 1;
    }

    // 2.1 初始化 Esp32Manager（MQTT + 任务调度）
    //     复用 DeviceManager 打开的 DB 句柄
    Esp32Manager esp32;
    if (!esp32.init(mgr.getDbHandle())) {
        fprintf(stderr, "[main] Esp32Manager 初始化失败\n");
        mgr.stop();
        return 1;
    }
    if (!esp32.start()) {
        fprintf(stderr, "[main] Esp32Manager 启动失败\n");
        mgr.stop();
        return 1;
    }

    // 3. 启动 IPC 客户端（连接 QT 和 main 的服务端）
    DeviceIpcClient ipc(mgr, esp32);
    if (!ipc.start()) {
        fprintf(stderr, "[main] IPC 客户端启动失败\n");
        esp32.stop();
        mgr.stop();
        return 1;
    }

    printf("[main] device_service 已启动（守护进程模式）\n");
    printf("[main] DB: %s\n", DB_PATH);
    printf("[main] 正在连接 QT 和 main 服务端...\n");

    // 4. 等待退出信号
    while (!g_exit_flag.load()) {
        pause();  // 阻塞直到信号到来
    }

    printf("[main] 收到退出信号，正在停止...\n");

    // 5. 清理
    ipc.stop();
    esp32.stop();
    mgr.stop();

    printf("[main] 已退出\n");
    return 0;
}

// ================================================================
// 单设备 Properties 测试模式：--test <ip> <token>
// ----------------------------------------------------------------
// 严格按 cuco.plug.v3 spec 表完整测试所有 Properties。
//   - R/W 属性：读 → 写(测试值) → 读 → 恢复 → 读，确保状态不变
//   - R   属性：只读
// 注：miio 事件推送需通过米家云端 MQTT 通道，本地 LAN 不接收，不测试。
// ================================================================

// 计算数值属性的测试值（在 min~max 范围内取一个不同于 orig 的值）
static int pickTestValue(int orig, int minv, int maxv) {
    int v = orig + 1;
    if (v > maxv) v = minv;
    if (v == orig) v = (orig == minv ? minv + 1 : orig - 1);
    return v;
}

static int runSingleTest(const std::string& ip, const std::string& token) {
    printf("\n=== 单设备测试模式（完整 spec 测试）===\n");
    printf("IP:    %s\n", ip.c_str());
    printf("Token: %s\n", token.c_str());

    miio::MiioClient client(ip, 54321, token);

    auto banner = [](const char* title) {
        printf("\n------------------------------------------------------------\n");
        printf("  %s\n", title);
        printf("------------------------------------------------------------\n");
    };

    auto getOne = [&](int siid, int piid, const char* label) -> nlohmann::json {
        nlohmann::json params = nlohmann::json::array({
            {{"siid", siid}, {"piid", piid}}
        });
        nlohmann::json resp;
        if (!client.rpcCall("get_properties", params, resp)) {
            printf("  [get] siid=%d piid=%d (%s) 失败\n", siid, piid, label);
            return nullptr;
        }
        auto result = resp.value("result", nlohmann::json::array());
        nlohmann::json val;
        if (result.is_array() && !result.empty() && result[0].contains("value")) {
            val = result[0]["value"];
        }
        printf("  [get] siid=%d piid=%d (%s): %s  => value=%s\n",
               siid, piid, label, result.dump().c_str(), val.dump().c_str());
        return val;
    };

    auto setOne = [&](int siid, int piid, const nlohmann::json& value, const char* label) -> bool {
        nlohmann::json params = nlohmann::json::array({
            {{"siid", siid}, {"piid", piid}, {"value", value}}
        });
        nlohmann::json resp;
        bool ok = client.rpcCall("set_properties", params, resp);
        auto result = resp.value("result", nlohmann::json::array());
        printf("  [set] siid=%d piid=%d (%s) value=%s: %s  => %s\n",
               siid, piid, label, value.dump().c_str(),
               ok ? "OK" : "FAIL", result.dump().c_str());
        return ok;
    };

    auto testBool = [&](int siid, int piid, const char* label) {
        auto orig = getOne(siid, piid, label);
        if (!orig.is_boolean()) {
            printf("  原值非 bool，跳过写测试\n");
            return;
        }
        setOne(siid, piid, !orig.get<bool>(), (std::string(label) + "-切换").c_str());
        getOne(siid, piid, (std::string(label) + "-切换后").c_str());
        setOne(siid, piid, orig, (std::string(label) + "-恢复").c_str());
        getOne(siid, piid, (std::string(label) + "-恢复后").c_str());
    };

    auto testInt = [&](int siid, int piid, const char* label, int minv, int maxv) {
        auto orig = getOne(siid, piid, label);
        if (!orig.is_number()) {
            printf("  原值非数字，跳过写测试\n");
            return;
        }
        int origv = orig.get<int>();
        int testv = pickTestValue(origv, minv, maxv);
        setOne(siid, piid, testv, (std::string(label) + "-改").c_str());
        getOne(siid, piid, (std::string(label) + "-改后").c_str());
        setOne(siid, piid, origv, (std::string(label) + "-恢复").c_str());
        getOne(siid, piid, (std::string(label) + "-恢复后").c_str());
    };

    auto testString = [&](int siid, int piid, const char* label) {
        auto orig = getOne(siid, piid, label);
        if (!orig.is_string()) {
            printf("  原值非 string，跳过写测试\n");
            return;
        }
        std::string origv = orig.get<std::string>();
        std::string testv = "test";
        setOne(siid, piid, testv, (std::string(label) + "-改").c_str());
        getOne(siid, piid, (std::string(label) + "-改后").c_str());
        setOne(siid, piid, origv, (std::string(label) + "-恢复").c_str());
        getOne(siid, piid, (std::string(label) + "-恢复后").c_str());
    };

    banner("连接设备（Hello 握手）");
    if (!client.connect()) {
        fprintf(stderr, "连接失败！请检查 IP/Token/网络\n");
        return 1;
    }

    banner("[0] miIO.info 设备信息");
    {
        nlohmann::json resp;
        if (client.rpcCall("miIO.info", {}, resp)) {
            printf("  miIO.info: %s\n", resp.dump().c_str());
        } else {
            printf("  miIO.info 失败\n");
        }
    }

    banner("[1] #2 switch 开关 (piid=1 on, piid=2 default-power-on, piid=3 fault)");
    testBool(2, 1, "开关 on");
    testInt(2, 2, "默认上电状态 default-power-on", 0, 2);
    getOne(2, 3, "故障 fault");

    banner("[2] #7 physical-controls-locked 物理控制锁 (piid=1 R/W)");
    testBool(7, 1, "物理控制锁");

    banner("[3] #11 power-consumption 功耗参数 (piid=1 耗电量, piid=2 电功率, 只读)");
    getOne(11, 1, "耗电量(0.01kWh)");
    getOne(11, 2, "电功率(W)");

    banner("[4] #13 indicator-light 指示灯 (piid=1 on R/W)");
    testBool(13, 1, "指示灯 on");

    banner("[5] #3 indicator-light 指示灯勿扰 (piid=2 mode, piid=3 start-time, piid=4 end-time, R/W)");
    testBool(3, 2, "勿扰模式 mode");
    testInt(3, 3, "勿扰开始时间 start-time", 0, 1440);
    testInt(3, 4, "勿扰结束时间 end-time", 0, 1440);

    banner("[6] #4 charging-protection 充电保护 (piid=1 on, piid=2 power, piid=3 protect-time, R/W)");
    testBool(4, 1, "充电保护开关 on");
    testInt(4, 2, "功率阈值 power", 2, 10);
    testInt(4, 3, "保护时长 protect-time", 1, 10);

    banner("[7] #5 cycle 循环任务 (piid=1 status R/W, piid=2 data-value R/W)");
    testBool(5, 1, "循环任务状态 status");
    testString(5, 2, "循环任务数据 data-value");

    banner("[8] #8 quick-countdown 快捷倒计时 (piid=1 on, piid=2 duration, piid=3 left-time, R/W)");
    testBool(8, 1, "倒计时开关 on");
    testInt(8, 2, "倒计时时长 duration", 0, 1440);
    testInt(8, 3, "倒计时剩余 left-time", 0, 1440);

    banner("[9] #9 max-power-limit 最大功率限制 (piid=1 on R/W, piid=2 power R/W)");
    testBool(9, 1, "最大功率开关 on");
    testInt(9, 2, "最大功率阈值 power", 1500, 2500);

    banner("[10] #10 over-use-ele-alert 过度用电提醒 (piid=1 on, piid=2 over-ele-day, piid=3 over-ele-month, R/W)");
    testBool(10, 1, "过用电提醒开关 on");
    testInt(10, 2, "过用电-日 over-ele-day", 1, 60);
    testInt(10, 3, "过用电-月 over-ele-month", 20, 1800);

    banner("[11] #12 on-off-count 开关次数/温度 (piid=1 开关次数, piid=2 温度, 只读)");
    getOne(12, 1, "开关次数 on-off-count");
    getOne(12, 2, "设备温度 temperature");

    banner("[12] #14 charge-prt-ext 充电保护参数扩展 (piid=1 power, piid=2 protect-time, R/W)");
    testInt(14, 1, "充电保护-功率阈值-ext power", 2, 600);
    testInt(14, 2, "充电保护-保护时长-ext protect-time", 1, 300);

    banner("[13] #15 power-limit-ext 功率限制参数扩展 (piid=1 power-ext R/W)");
    testInt(15, 1, "功率限制-ext power-ext", 300, 2500);

    printf("\n=== 测试完成 ===\n");
    return 0;
}

// ================================================================
// main
// ================================================================
int main(int argc, char** argv)
{
    // 调试模式：--test <ip> <token>
    if (argc == 4 && std::string(argv[1]) == "--test") {
        return runSingleTest(argv[2], argv[3]);
    }

    // 默认守护进程模式
    return runDaemon();
}
