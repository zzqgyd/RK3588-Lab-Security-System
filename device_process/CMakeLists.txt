cmake_minimum_required(VERSION 3.10)
project(device_service)

# ==================== 安装路径 ====================
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install)

# ==================== 编译选项 ====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_FLAGS "-g -O0")
set(CMAKE_C_FLAGS "-g -O0")

# ==================== RPATH：运行时自动从 ./lib 找库 ====================
set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

# ==================== 项目根目录（用于引用 ipc/） ====================
get_filename_component(PROJECT_ROOT ${CMAKE_SOURCE_DIR}/.. ABSOLUTE)

# ==================== 依赖查找 ====================
# OpenSSL：提供 AES-128-CBC 加解密 + MD5
find_package(OpenSSL REQUIRED)
# nlohmann_json：JSON 解析（apt install nlohmann-json3-dev）
find_package(nlohmann_json 3.2 REQUIRED)
# Paho MQTT C++库（apt install libpaho-mqttpp3-dev libpaho-mqtt-dev）
find_library(PAHO_MQTT_CPP_LIB paho-mqttpp3)
find_library(PAHO_MQTT_C_LIB   paho-mqtt3as)
if(NOT PAHO_MQTT_CPP_LIB)
    message(FATAL_ERROR "Paho MQTT C++ library not found (libpaho-mqttpp3-dev)")
endif()

# ==================== 头文件路径 ====================
# 注意：源码中 #include "ipc/xxx.h" 带前缀，因此需要把项目根目录加入
#       include 路径，使其能解析为 ${PROJECT_ROOT}/ipc/xxx.h
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${PROJECT_ROOT}              # 复用主项目的 ipc/ipc_socket.h, ipc/db.h
    ${OPENSSL_INCLUDE_DIR}
)

# ==================== 源文件 ====================
set(SOURCES
    ${CMAKE_SOURCE_DIR}/src/main.cpp
    ${CMAKE_SOURCE_DIR}/src/miio/miio_client.cpp
    ${CMAKE_SOURCE_DIR}/src/devices/miot_plug.cpp
    ${CMAKE_SOURCE_DIR}/src/device_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/device_ipc_client.cpp
    ${CMAKE_SOURCE_DIR}/src/mqtt_client.cpp
    ${CMAKE_SOURCE_DIR}/src/esp32_manager.cpp
    ${PROJECT_ROOT}/ipc/db.cpp          # 复用主项目的 DB 接口
    ${PROJECT_ROOT}/ipc/ipc_socket.cpp  # 复用主项目的 socket 接口
)

add_executable(${PROJECT_NAME} ${SOURCES})

# ==================== 链接库 ====================
target_link_libraries(${PROJECT_NAME}
    ${OPENSSL_CRYPTO_LIBRARY}   # libcrypto：AES + MD5
    nlohmann_json::nlohmann_json
    ${PAHO_MQTT_CPP_LIB}        # Paho MQTT C++
    ${PAHO_MQTT_C_LIB}          # Paho MQTT C（依赖）
    sqlite3                     # 共用业务数据库 records.db
    pthread
    rt
)

# ==================== 安装规则 ====================
install(TARGETS ${PROJECT_NAME} DESTINATION ./)
