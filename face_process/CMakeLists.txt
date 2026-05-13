cmake_minimum_required(VERSION 3.10)
project(face_service)

set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "-g -O0")
set(CMAKE_CXX_FLAGS "-g -O0")

set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

find_package(OpenCV REQUIRED)

include_directories(
    ${CMAKE_SOURCE_DIR}/..  #ipc
    include
    ../utils
    ../3rdparty/rknn/include
    ../3rdparty/rga/include
    ../3rdparty/InspireCV/include
    ../3rdparty/
    ../3rdparty/turbojpeg/include
    ${OpenCV_INCLUDE_DIRS}
)

link_directories(../lib)

set(SOURCES
    src/main.cpp
    src/v4l2_camera.cpp
    src/face_module.cpp 
    
    ../utils/image_utils.c
    ../utils/file_utils.c

    # ../ipc/ipc_shm.cpp
    ../ipc/ipc_socket.cpp
    ../ipc/db.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME}
    rknnrt
    ${OpenCV_LIBS}
    sqlite3
    rga
    rockchip_mpp
    InspireFace
    turbojpeg 
    pthread
    dl
    rt
    ${OpenCV_LIBS} 
)

install(TARGETS ${PROJECT_NAME} DESTINATION ./)

# 安装模型
install(DIRECTORY model/ DESTINATION model/)

# 安装依赖库
install(FILES
    ../lib/librknnrt.so ../lib/librga.so ../lib/librockchip_mpp.so
    ../lib/libturbojpeg.so ../lib/libInspireFace.so
    ../lib/libturbojpeg.so.0
    DESTINATION lib
)