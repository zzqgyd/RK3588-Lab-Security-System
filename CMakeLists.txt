cmake_minimum_required(VERSION 3.10)
project(gst_mpp_rga_yolo)

set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install)

# 板端原生编译，不需要交叉编译器！
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 99)

set(CMAKE_CXX_FLAGS "-g -O0")
set(CMAKE_C_FLAGS "-g -O0")

# 运行时找 lib 目录
set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

# 自动找依赖库（glib/gstreamer）
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-app-1.0)

# 头文件
include_directories(
    include
    utils
    3rdparty/rknn/include
    3rdparty/rga/include
    3rdparty/mpp/include
    3rdparty/turbojpeg/include

    ${GLIB_INCLUDE_DIRS}
    ${GST_INCLUDE_DIRS}
)

# 库路径
link_directories(lib)

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

add_executable(${PROJECT_NAME} ${SOURCES})

# 链接
target_link_libraries(${PROJECT_NAME}
    rknnrt
    rga
    rockchip_mpp
    ${GST_LIBRARIES}
    ${GLIB_LIBRARIES}
    turbojpeg
    pthread
    dl
    m
)

# 安装
install(TARGETS ${PROJECT_NAME} DESTINATION ./)
install(FILES
    lib/librknnrt.so
    lib/librga.so
    lib/librockchip_mpp.so
    lib/libturbojpeg.so
    DESTINATION lib
)

# 安装模型
install(DIRECTORY model/ DESTINATION model/)

#安装配置文件
install(FILES ${CMAKE_SOURCE_DIR}/config.ini DESTINATION config/)