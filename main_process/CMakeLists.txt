cmake_minimum_required(VERSION 3.10)
project(gst_mpp_rga_yolo)

# ==================== 安装路径 ====================
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install)

# ==================== 编译选项 ====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_FLAGS "-g -O0")
set(CMAKE_C_FLAGS "-g -O0")

# ==================== RPATH：运行时自动从./lib找库 ====================
set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

# ==================== pkg-config 查找依赖 ====================
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GST  REQUIRED gstreamer-1.0 gstreamer-app-1.0 gstreamer-allocators-1.0)

# 注意：只检查FFmpeg，不自动链接
pkg_check_modules(AV REQUIRED libavformat libavcodec libavutil)

# ==================== 头文件路径 ====================
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/../utils
    ${CMAKE_SOURCE_DIR}/../3rdparty/rknn/include
    ${CMAKE_SOURCE_DIR}/../3rdparty/rga/include
    ${CMAKE_SOURCE_DIR}/../3rdparty/mpp/include
    ${CMAKE_SOURCE_DIR}/../3rdparty/gstreamer-1.0/include
    ${CMAKE_SOURCE_DIR}/../3rdparty/turbojpeg/include
    ${CMAKE_SOURCE_DIR}/../3rdparty/InspireCV/include
    ${GLIB_INCLUDE_DIRS}
    ${AV_INCLUDE_DIRS}
)

# ==================== 库文件搜索路径 ====================
link_directories(
    ${CMAKE_SOURCE_DIR}/../lib
    /usr/lib/aarch64-linux-gnu
)

# ==================== 源文件 ====================
set(SOURCES
    ${CMAKE_SOURCE_DIR}/src/main.cpp
    ${CMAKE_SOURCE_DIR}/src/gst_decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/scheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/worker_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/yolov5_infer.cc
    ${CMAKE_SOURCE_DIR}/src/postprocess.cc
    ${CMAKE_SOURCE_DIR}/src/detection_state.cpp
    ${CMAKE_SOURCE_DIR}/src/roi_config.cpp
    ${CMAKE_SOURCE_DIR}/src/video_recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/recorder_queue.cpp
    ${CMAKE_SOURCE_DIR}/../ipc/ipc_socket.cpp
    ${CMAKE_SOURCE_DIR}/../ipc/db.cpp
    ${CMAKE_SOURCE_DIR}/../utils/image_utils.c
    ${CMAKE_SOURCE_DIR}/../utils/file_utils.c
)

add_executable(${PROJECT_NAME} ${SOURCES})

# ==================== 链接库（关键：顺序很重要） ====================
target_link_libraries(${PROJECT_NAME}
    # RK3588 硬件加速
    rknnrt
    rga
    rockchip_mpp
    
    # 人脸识别
    InspireFace
    
    # GStreamer
    ${GST_LIBRARIES}
    ${GLIB_LIBRARIES}
    
    # 图像编解码
    turbojpeg
    
    # 系统库
    pthread
    sqlite3
    rt
    dl
    m
    
    # 硬件驱动
    drm
    
    # ========== FFmpeg 库（顺序照搬你的 demo）==========
    avformat
    avcodec
    avutil
)

# ==================== 安装规则 ====================
install(TARGETS ${PROJECT_NAME} DESTINATION ./)

install(FILES
    ${CMAKE_SOURCE_DIR}/../lib/librknnrt.so
    ${CMAKE_SOURCE_DIR}/../lib/librga.so
    ${CMAKE_SOURCE_DIR}/../lib/librockchip_mpp.so
    ${CMAKE_SOURCE_DIR}/../lib/libturbojpeg.so
    ${CMAKE_SOURCE_DIR}/../lib/libturbojpeg.so.0
    ${CMAKE_SOURCE_DIR}/../lib/libInspireFace.so
    DESTINATION lib
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/model/ DESTINATION model/)