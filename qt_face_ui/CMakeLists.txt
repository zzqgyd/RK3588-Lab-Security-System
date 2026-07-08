cmake_minimum_required(VERSION 3.16)
project(QtFaceUI VERSION 1.0.0)

# ================================================================
# Qt 配置
# ================================================================
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 查找 Qt5 模块（需要 OpenGL 支持）
find_package(Qt5 REQUIRED COMPONENTS
    Core
    Widgets
    Gui
    OpenGL
    Sql
    Network
    Charts
    Concurrent
)

# ================================================================
# 头文件路径
# ================================================================
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ================================================================
# 源文件
# ================================================================
set(SOURCES
    main.cpp
    app/application.cpp
    windows/hdmi_window.cpp
    windows/dsi_window.cpp
    ipc/frame_receiver.cpp
    ipc/face_ipc_server.cpp
    ipc/device_ipc_client.cpp
    ../ipc/ipc_socket.cpp
    widgets/gl_stream_view.cpp
    widgets/nv12_texture.cpp
    widgets/yuyv_texture.cpp
    widgets/yuv_shader.cpp
    widgets/keyboard_drag_helper.cpp
    db/db_manager.cpp
    windows/face_list_window.cpp
    windows/attendance_window.cpp
    windows/device_usage_window.cpp
    windows/video_record_window.cpp
    windows/statistics_window.cpp
    windows/system_dashboard.cpp
    windows/roi_config_window.cpp
    widgets/roi_edit_view.cpp
    windows/device_manage_window.cpp
)

set(HEADERS
    app/constants.h
    app/application.h
    windows/hdmi_window.h
    windows/dsi_window.h
    ipc/frame_receiver.h
    ipc/face_ipc_server.h
    ipc/device_ipc_client.h
    widgets/gl_stream_view.h
    widgets/nv12_texture.h
    widgets/yuyv_texture.h
    widgets/yuv_shader.h
    widgets/keyboard_drag_helper.h
    db/db_manager.h
    windows/face_list_window.h
    windows/attendance_window.h
    windows/device_usage_window.h
    windows/video_record_window.h
    windows/statistics_window.h
    windows/system_dashboard.h
    windows/roi_config_window.h
    widgets/roi_edit_view.h
    windows/device_manage_window.h
)

# ================================================================
# 可执行文件
# ================================================================
add_executable(qt_face_ui ${SOURCES} ${HEADERS})

# 添加项目根目录到 include 路径（用于访问公共头文件 ipc/ipc_socket.h、ipc/db.h）
target_include_directories(qt_face_ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# 链接 Qt5 库
target_link_libraries(qt_face_ui
    Qt5::Core
    Qt5::Widgets
    Qt5::Gui
    Qt5::OpenGL
    Qt5::Sql
    Qt5::Network
    Qt5::Charts
    Qt5::Concurrent
)

# 链接 OpenGL 库（RK3588 需要）
find_package(OpenGL REQUIRED)
target_link_libraries(qt_face_ui ${OPENGL_LIBRARIES})

# 安装
install(TARGETS qt_face_ui RUNTIME DESTINATION bin)

# ================================================================
# 打印配置信息
# ================================================================
message(STATUS "=========================================")
message(STATUS "QtFaceUI Configuration:")
message(STATUS "  Source Dir: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "  Qt Version: ${Qt5_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  OpenGL: ${OPENGL_LIBRARIES}")
message(STATUS "=========================================")