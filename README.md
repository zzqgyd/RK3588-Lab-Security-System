# 基于 RK3588 的实验室多路智能安防人员管理系统

面向实验室场景的多路智能安防与人员管理系统，运行于 RK3588 平台。系统采用四进程分布式架构，集成 YOLOv5 目标检测、InspireFace 人脸识别、米家智能插座控制与 ESP32 摄像头推流，实现人员考勤、设备使用登记、自动录像、智能通断电等闭环功能。全链路采用 DMA-BUF 零拷贝技术，充分发挥 RK3588 的 NPU/GPU/VPU 硬件加速能力。

## 系统架构

### 四进程架构

| 进程 | 二进制名 | 职责 |
|------|---------|------|
| `main_process` | `gst_mpp_rga_yolo` | 主进程：GStreamer 多路 RTSP 硬解、YOLOv5 NPU 推理、检测状态机、FFmpeg 硬件编码录像 |
| `qt_face_ui` | `qt_face_ui` | Qt 双屏 UI：HDMI 大屏 8 路监控显示、DSI 触摸屏交互控制 |
| `face_process` | `face_service` | 人脸识别：InspireFace NPU 推理、USB 摄像头采集、RTSP 拉流识别 |
| `device_process` | `device_service` | 设备管理：米家插座 miio 协议控制、ESP32 MQTT 调度、软件倒计时 |

### IPC 通信拓扑

四进程通过 8 条 Unix Socket 通道通信，帧数据采用 SCM_RIGHTS 传递 DMA-BUF fd 实现零拷贝：

```
┌─────────────┐  ①NV12帧(DMA-BUF)   ┌─────────────┐
│ main_process│ ───────────────────→ │  qt_face_ui │
│  (8路解码+   │  ④ESP32识别事件/结果  │  (双屏显示+   │
│   YOLO推理)  │ ←──────────────────→ │   触摸交互)  │
└──────┬───────┘                      └──────┬───────┘
       │ ④ESP32识别事件/结果                  │ ③人脸命令
       │ ←──────────────────→ ┌─────────────┐│ ──────────→ ┌─────────────┐
       │                      │ face_process││             │ device_process│
       │ ⑧Tag协议(登记/释放)   │ (InspireFace││ ⑦查询/管理   │ (米家插座+   │
       └──────────────────→ └─────────────┘│ ←──────────→ │  ESP32调度)  │
                                             │ ⑧Tag协议     │ (MQTT→ESP32) │
                                             └─────────────┴─────────────┘
```

**Socket 通道说明：**

| # | 路径 | 服务端 | 客户端 | 用途 |
|---|------|--------|--------|------|
| ① | `/tmp/sock_main_qt` | Qt | main | 8路 NV12 监控画面 + 检测框 + 设备占用状态 |
| ② | `/tmp/sock_face_qt` | Qt | face | USB 摄像头 YUYV 画面 |
| ③ | `/tmp/sock_qt_face` | Qt | face | 签到/签退/登记/录入/删除命令 |
| ④ | `/tmp/sock_main_face` | main | face | ESP32 识别事件下发 / 识别结果回传 |
| ⑤ | `/tmp/sock_main_roi` | main | Qt | ROI 配置热重载（发 'R' 等 'O'） |
| ⑥ | `/tmp/sock_qt_main` | main | Qt | 手动断电等命令 |
| ⑦ | `/tmp/sock_qt_device` | Qt | device | 插座/ESP32 查询与增删管理 |
| ⑧ | `/tmp/sock_main_device` | main | device | 双向 Tag 协议（登记/释放/识别结果） |

**启动顺序：** Qt → main_process → face_process / device_process

## 核心功能

### 1. 多路目标检测与智能录像

- **8 路 RTSP 同时解码**：GStreamer `mppvideodec` 硬件解码 NV12，DMA-BUF fd 零拷贝传递
- **YOLOv5 NPU 推理**：RKNN 三核并行（`RKNN_NPU_CORE_0/1/2`），预分配内存零拷贝输入输出
- **ROI 区域检测**：可配置每路流的检测区域，人在区域内停留超阈值才触发识别
- **智能录像**：FFmpeg `h264_rkmpp` 硬件编码，DRM_PRIME fd 零拷贝输入，有人自动录像、离开停止

### 2. 人脸识别（三种登记模式）

- **USB 主动登记**：用户在 DSI 触摸屏操作 → USB 摄像头采集 → InspireFace NPU 识别 → 开插座 + 标记"使用中"
- **ESP32 被动登记**：YOLO 检测有人停留 → ESP32 摄像头推流 → 人脸识别 → 自动开插座
- **ESP32 主动登记**：用户在 ESP32 端按键 → MQTT 请求 → 拉流识别 → 自动开插座
- 识别成功后设备显示"使用中"并在指定时长内抑制重复检测，到期自动恢复
- 无插排设备照常登记使用，仅跳过开插座步骤

### 3. 智能插座控制

- **米家智能插座3**（cuco.plug.v3）MIOT Spec 协议
- 自实现 miio 协议（UDP 54321 + AES-128-CBC + MD5 校验），不依赖米家 SDK
- 识别成功自动开插座 + 软件倒计时，到期自动断电
- 过载（2000W）/过温（75℃）自动断电保护
- 支持多房间多插座管理，QT 界面增删配置

### 4. ESP32 摄像头对接

- ESP32 通过 MQTT 与 device_process 通信，broker 运行于 RK3588 本地（端口 1883）
- 被动登记需用户按 ESP32 按钮确认后才开始 RTSP 推流（30 秒超时取消）
- 心跳机制：ESP32 每 30 秒上报心跳，60 秒未收到标记离线
- RTSP URL 在 QT 设备管理界面配置，存入数据库

### 5. Qt 双屏交互

- **HDMI 大屏**：8 路 2×4 网格 / 单路全屏切换，OpenGL 渲染，显示检测框与 ROI 区域
- **DSI 触摸屏**：12 个功能入口（签到/签退/设备登记/人脸录入/考勤记录/设备使用/录像记录/人脸库/显示控制/ROI配置/数据统计/系统状态/设备管理）
- 自带 Google 拼音输入法，支持中文输入

## 目录结构

```
21ROI加/
├── main_process/          # 主进程：YOLO检测 + GStreamer解码 + 录像
│   ├── src/               # 10个源文件（main/gst_decoder/scheduler/worker_pool/yolov5_infer...）
│   ├── include/           # 16个头文件（队列/内存池/状态机/录像器...）
│   └── CMakeLists.txt
├── qt_face_ui/            # Qt双屏UI进程
│   ├── app/               # FaceApplication 总控
│   ├── windows/           # 12个窗口类（HDMI/DSI/考勤/设备管理/ROI配置...）
│   ├── ipc/               # FrameReceiver/FaceIpcServer/DeviceIpcClient
│   ├── widgets/           # OpenGL渲染/NV12纹理/YUYV纹理/ROI编辑
│   ├── db/                # DbManager
│   └── resources/         # 拼音词库
├── face_process/          # 人脸识别进程
│   ├── src/               # main/face_module/v4l2_camera
│   ├── include/           # RecognitionEngine/DisplaySender/CommunicationLayer
│   ├── model/             # Gundam_RK3588 人脸模型
│   └── install/           # 编译产物 + 依赖库
├── device_process/        # 设备管理进程
│   ├── src/               # main/device_manager/esp32_manager/mqtt_client/miot_plug/miio_client
│   ├── include/           # IotDevice抽象/MiotPlug/Esp32Manager/MqttClient/DeviceIpcClient
│   └── CMakeLists.txt
├── ipc/                   # 共享IPC基础设施
│   ├── ipc_socket.h/cpp   # 8条Socket路径 + 消息结构体 + 帧传输接口
│   └── db.h/cpp           # SQLite表结构 + CRUD接口
├── config/                # 配置文件
│   ├── config.ini         # YOLO模型路径 + 8路RTSP地址
│   ├── roi.conf           # 8路ROI区域配置
│   ├── records.db         # 业务数据库（考勤/设备使用/录像/插座/ESP32）
│   └── face_database.db   # 人脸特征库
├── MQTT_ESP32/            # MQTT控制器测试程序
├── esp32_simulator/       # ESP32模拟器（无硬件时测试MQTT流程）
└── diagrams/              # 架构流程图
```

## 数据库

SQLite WAL 模式，多进程安全读写。两个库文件位于 `config/`：

| 表名 | 写入进程 | 用途 |
|------|---------|------|
| `attendance` | face_process | 考勤记录（签到/签退） |
| `device_usage` | face_process | 设备使用登记记录 |
| `video_records` | main_process | 录像文件记录 |
| `face_mapping` | face_process | 人脸特征 ID ↔ 姓名映射 |
| `device_plugs` | device_process | 智能插座配置（room_id + device_id 唯一） |
| `room_esp32` | device_process | ESP32 配置（每房间一个，含 RTSP URL） |

## 零拷贝优化

系统在三个关键路径实现零拷贝，避免大块图像数据在用户态复制：

1. **解码 → 推理**：GStreamer `mppvideodec` 输出 DMA-BUF fd，通过 `gst_dmabuf_memory_get_fd` 提取，直接写入 RKNN 预分配内存（`rknn_set_io_mem`），无需 memcpy
2. **推理 → 显示**：主进程通过 `ipc_sock_send_frame` 以 SCM_RIGHTS 传递 DMA-BUF fd 给 Qt，Qt OpenGL 直接采样该 fd 渲染
3. **解码 → 录像**：DMA-BUF fd 直接作为 FFmpeg `AV_PIX_FMT_DRM_PRIME` 输入 `h264_rkmpp` 硬件编码器

## 硬件依赖

- **主控**：RK3588（8核 ARM Cortex-A76/A55，6 TOPS NPU）
- **摄像头**：8 路 RTSP 网络摄像头（H.264）
- **USB 摄像头**：V4L2 设备 `/dev/video21`（YUYV 640×360）
- **显示**：HDMI 大屏 + DSI 触摸屏（双屏异显）
- **智能插座**：米家智能插座3（cuco.plug.v3），UDP 54321 端口
- **ESP32**：ESP32-S3 + OV2640 摄像头，RTSP 推流
- **MQTT Broker**：Mosquitto 运行于 RK3588 本地，端口 1883

## 编译与运行

### 依赖

- **交叉编译环境**：RK3588 ARM64（aarch64）工具链
- **Rockchip SDK**：RKNPU（librknnrt）、RGA（librga）、MPP（librockchip_mpp）、DRM
- **第三方库**：GStreamer、FFmpeg（含 rkmpp）、OpenCV、Qt5、Paho MQTT、nlohmann_json、OpenSSL、SQLite3、InspireFace
- **模型文件**：YOLOv5s RKNN 模型（`model/RK3588/yolov5s-640-640.rknn`）、InspireFace Gundam_RK3588 人脸模型

### 编译

每个进程独立编译，在 RK3588 上执行：

```bash
# 主进程
cd main_process && mkdir build && cd build
cmake .. && make -j8

# 人脸进程
cd face_process && mkdir build && cd build
cmake .. && make -j8

# 设备进程
cd device_process && mkdir build && cd build
cmake .. && make -j8

# Qt UI
cd qt_face_ui && mkdir build && cd build
cmake .. && make -j8
```

### 运行

按启动顺序执行（运行目录为各进程的 `install/` 或 `build/`）：

```bash
# 1. 启动 Qt UI（创建所有 IPC 服务端 socket）
./qt_face_ui &

# 2. 启动主进程（连接 Qt，创建 4 个服务端 socket）
./gst_mpp_rga_yolo &

# 3. 启动人脸进程（连接 Qt 和主进程）
./face_service &

# 4. 启动设备进程（连接 Qt 和主进程）
./device_service &

# 5. 确保 MQTT Broker 运行
mosquitto -p 1883 &
```

### 配置

- 编辑 `config/config.ini` 配置 8 路 RTSP 流地址和 YOLO 模型路径
- 编辑 `config/roi.conf` 配置每路流的 ROI 检测区域（`stream_id enabled x y w h`）
- 在 Qt 设备管理界面新增智能插座（IP + Token）和 ESP32（RTSP URL）

## 技术特点

- **全链路零拷贝**：DMA-BUF fd 从解码→推理→显示→录像全程传递，无 memcpy
- **NPU 三核并行**：YOLOv5 推理按 `worker_id % 3` 绑定 NPU 核心，多 Worker 共享权重
- **无锁队列**：SPSC RingBuffer + MPMC 无锁队列，多路帧高效调度
- **对象池**：Frame 对象池 + 自定义删除器，避免频繁内存分配
- **Tag 协议**：main↔device 单连接复用，4字节 tag 前缀区分消息类型，避免 MSG_PEEK 死锁
- **锁分离 RPC**：米家插座 RPC 调用锁外执行，避免长耗时操作阻塞 IPC 查询
- **软件倒计时**：设备使用时长由软件计时，不依赖插座硬件倒计时功能
- **状态机驱动**：设备检测状态机管理人在/离开/停留/免打扰，自动触发识别与录像

## License

本项目仅供学习和研究使用。
