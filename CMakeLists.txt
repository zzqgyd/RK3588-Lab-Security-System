cmake_minimum_required(VERSION 3.10)
project(gst_mpp_rga_yolo)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 99)

set(CMAKE_CXX_FLAGS "-g -O0 ")
set(CMAKE_C_FLAGS "-g -O0 ")

# 安装路径
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install)

# RPATH 设置（运行时自动找 lib 目录）
set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

# 架构
set(LIB_ARCH aarch64)

# 头文件路径
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/utils
    ${CMAKE_SOURCE_DIR}/3rdparty/rknn/include
    ${CMAKE_SOURCE_DIR}/3rdparty/rga/include
    ${CMAKE_SOURCE_DIR}/3rdparty/mpp/include
)

# GStreamer 头文件
include_directories(
    /home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot/usr/include/gstreamer-1.0
    /home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot/usr/include/glib-2.0
    /home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot/usr/lib/glib-2.0/include
)

# 库文件路径
link_directories(
    ${CMAKE_SOURCE_DIR}/lib
    /home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot/usr/lib
)

# 源文件
set(SOURCES
    src/demo.cc
    src/gst_decoder.c
    src/gst_display.c
    src/postprocess.cc
    src/yolov5_infer.cc
    utils/image_utils.c
    utils/file_utils.c
    utils/image_drawing.c
)

# 生成可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接库（标准写法：不加 lib 和 .so）
target_link_libraries(${PROJECT_NAME}
    rknnrt
    rga
    rockchip_mpp
    gstreamer-1.0
    gstapp-1.0
    glib-2.0
    gobject-2.0
    gmodule-2.0
    turbojpeg
    pthread
    dl
    m
)

# 安装（自动复制所有库）
install(TARGETS ${PROJECT_NAME} DESTINATION ./)

# 自动安装依赖库
install(FILES
    ${CMAKE_SOURCE_DIR}/lib/librknnrt.so
    ${CMAKE_SOURCE_DIR}/lib/librga.so
    ${CMAKE_SOURCE_DIR}/lib/librockchip_mpp.so
    /home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot/usr/lib/libturbojpeg.so
    /home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot/usr/lib/libturbojpeg.so.0
    /home/elf/aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot/usr/lib/libturbojpeg.so.0.2.0
    DESTINATION lib
)

# 安装模型
install(DIRECTORY model/ DESTINATION model/)