cmake_minimum_required(VERSION 3.10)
project(InspireCV)

# == OpenCV Backend Mode ==
# OpenCV Backend Mode:
# When enabled, InspireCV will use OpenCV as the underlying image processing engine.
# This provides access to OpenCV's extensive functionality and optimizations.
# When disabled, InspireCV will use its own lightweight OKCV backend implementation.
# OKCV provides basic image processing capabilities with minimal dependencies.
option(INSPIRECV_BACKEND_OPENCV "If you want to use OpenCV as the CV backend" OFF)

# == OKCV Backend Mode ==
# OKCV Backend Mode:
# When enabled, InspireCV will use its own lightweight OKCV backend implementation.
# OKCV provides basic image processing capabilities with minimal dependencies.
# When using the OKCV backend, you can optionally enable OpenCV's I/O and GUI APIs for debugging convenience.
# This allows you to use OpenCV's image reading/writing functions and visualization tools like imshow() 
# while still using OKCV as the core processing engine.
# These OpenCV features are typically enabled during development to help with debugging and visualization.
option(INSPIRECV_BACKEND_OKCV_USE_OPENCV "Turn on some OpenCV api support inside OKCV" OFF)
option(INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO "Let okcv internally use OpenCV's image codec and read-write api." OFF)
option(INSPIRECV_BACKEND_OKCV_USE_OPENCV_GUI "Make okcv internally dependent on some of OpenCV's curtain functions, like imshow." OFF)

option(INSPIRECV_BUILD_OBJECT_LIBS "Build object libs" ON)
option(INSPIRECV_BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(INSPIRECV_BUILD_TESTS "Build tests" OFF)
option(INSPIRECV_BUILD_SAMPLE "Build sample" OFF)

option(INSPIRECV_EXPANSION_MODULE_TEST_MNN_IMGPROCESS "Optionally enable MNN image processing module test" OFF)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
set(CMAKE_CXX_STANDARD 14)
add_compile_options(-fPIC)

if(WIN32)
  if(INSPIRECV_BUILD_SHARED_LIBS)
    add_definitions(-DINSPIRECV_API=__declspec\(dllexport\))
  endif()
else()
  add_definitions(-DINSPIRECV_API=__attribute__\(\(visibility\(\"default\"\)\)\))
endif()

set(EIGEN3_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/Eigen-3.4.0-Headers")

if(INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO OR INSPIRECV_BACKEND_OKCV_USE_OPENCV_GUI)
    set(INSPIRECV_BACKEND_OKCV_USE_OPENCV ON)
endif()

set(NEED_LINK_LIBS "")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_BUILD_TYPE "Release")
else()
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(OTHER_SUFFIX "")
if(INSPIRECV_BACKEND_OKCV_USE_OPENCV)
    find_package(OpenCV REQUIRED)
    add_definitions("-DINSPIRECV_BACKEND_OKCV_USE_OPENCV")
    if (INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO)
        add_definitions("-DINSPIRECV_BACKEND_OKCV_USE_OPENCV_IO")
        set(OTHER_SUFFIX "${OTHER_SUFFIX}@OpenCV-IO")
    else()
        set(OTHER_SUFFIX "${OTHER_SUFFIX}@OpenCV")
    endif()
    if (INSPIRECV_BACKEND_OKCV_USE_OPENCV_GUI)
        add_definitions("-DINSPIRECV_BACKEND_OKCV_USE_OPENCV_GUI")
        set(OTHER_SUFFIX "${OTHER_SUFFIX}@OpenCV-GUI")
    endif()
    set(NEED_LINK_LIBS ${NEED_LINK_LIBS} ${OpenCV_LIBS})

endif()

set(INSPIRECV_BACKEND "OKCV")
if(INSPIRECV_BACKEND_OPENCV)
    set(INSPIRECV_BACKEND "OpenCV")
    message(STATUS "InspireCV Backend: ${INSPIRECV_BACKEND}")
    add_definitions("-DINSPIRECV_BACKEND_OPENCV")
    find_package(OpenCV REQUIRED)
    set(NEED_LINK_LIBS ${NEED_LINK_LIBS} ${OpenCV_LIBS})
endif()

# Add version information
set(INSPIRECV_VERSION_MAJOR 0)
set(INSPIRECV_VERSION_MINOR 6)
set(INSPIRECV_VERSION_PATCH 0)
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d")

set(VERSION_SUFFIX "")
if(INSPIRECV_BACKEND STREQUAL "OKCV")
    if(INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO OR INSPIRECV_BACKEND_OKCV_USE_OPENCV_GUI OR INSPIRECV_BACKEND_OKCV_USE_OPENCV)
        set(VERSION_SUFFIX "@General(With OpenCV)")
    else()
        set(VERSION_SUFFIX "@General")
    endif()
else()
    set(VERSION_SUFFIX "@OpenCV")
endif()

set(INSPIRECV_VERSION_STRING 
    "InspireCV v${INSPIRECV_VERSION_MAJOR}.${INSPIRECV_VERSION_MINOR}.${INSPIRECV_VERSION_PATCH}${VERSION_SUFFIX} - Build Time: ${BUILD_TIMESTAMP}")

set(INSPIRECV_BACKEND_STRING "")
if(INSPIRECV_BACKEND STREQUAL "OpenCV")
    set(INSPIRECV_BACKEND_STRING "OpenCV")
else()
    set(INSPIRECV_BACKEND_STRING "OKCV")
endif()

set(EXTENSION_LIBS "")
if(INSPIRECV_EXPANSION_MODULE_TEST_MNN_IMGPROCESS)
    add_definitions("-DINSPIRECV_EXPANSION_MODULE_TEST_MNN_IMGPROCESS")
    message(STATUS "================================")
    message(STATUS "Enable MNN Image Processing Module Test")
    message(STATUS "MNN_STATIC_LIBS: ${MNN_STATIC_LIBS}")
    message(STATUS "MNN_INCLUDE_DIRS: ${MNN_INCLUDE_DIRS}")
    include_directories(${MNN_INCLUDE_DIRS})
    set(EXTENSION_LIBS ${EXTENSION_LIBS} ${MNN_STATIC_LIBS})
    if(NOT MNN_STATIC_LIBS OR NOT MNN_INCLUDE_DIRS)
        message(FATAL_ERROR "MNN_STATIC_LIBS and MNN_INCLUDE_DIRS must be set when INSPIRECV_EXPANSION_MODULE_TEST_MNN_IMGPROCESS is ON")
    endif()
    message(STATUS "================================")
endif()

    
# Start of the project
set(INSPIRECV_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(INSPIRECV_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/inspirecv)
set(CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/)
set(OKCV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/inspirecv/okcv)

# configure the version.h file
configure_file(${INSPIRECV_SRC_DIR}/version.cpp.in ${INSPIRECV_SRC_DIR}/version.cpp)

include_directories(${EIGEN3_INCLUDE_DIRS})
include_directories(${INSPIRECV_SRC_DIR})
include_directories(${INSPIRECV_INCLUDE_DIRS})
include_directories(${INSPIRECV_INCLUDE_DIRS}/inspirecv)
include_directories(${CPP_DIR})

if(INSPIRECV_BACKEND STREQUAL "OKCV")
    file(GLOB_RECURSE OKCV_SRC_FILES "${OKCV_DIR}/*.cpp")
    set(INSPIRECV_SRC_FILES ${INSPIRECV_SRC_FILES} ${OKCV_SRC_FILES})
endif()


set(INSPIRECV_SRC_FILES ${INSPIRECV_SRC_FILES} 
    ${INSPIRECV_SRC_DIR}/impl/point.cpp
    ${INSPIRECV_SRC_DIR}/impl/rect.cpp
    ${INSPIRECV_SRC_DIR}/impl/size.cpp
    ${INSPIRECV_SRC_DIR}/impl/transform_matrix.cpp
    ${INSPIRECV_SRC_DIR}/impl/image.cpp
    )

set(INSPIRECV_SRC_FILES ${INSPIRECV_SRC_FILES} 
    ${INSPIRECV_SRC_DIR}/costman.cpp
    ${INSPIRECV_SRC_DIR}/logging.cpp
    ${INSPIRECV_SRC_DIR}/version.cpp
    )

# message(STATUS "INSPIRECV_SRC_FILES: ${INSPIRECV_SRC_FILES}")

if (INSPIRECV_BUILD_SAMPLE)    
    add_executable(sample_point ${CPP_DIR}/sample/sample_point.cpp ${INSPIRECV_SRC_FILES})
    target_link_libraries(sample_point ${NEED_LINK_LIBS} ${EXTENSION_LIBS})
    add_executable(sample_image ${CPP_DIR}/sample/sample_image.cpp ${INSPIRECV_SRC_FILES})
    target_link_libraries(sample_image ${NEED_LINK_LIBS} ${EXTENSION_LIBS})
    add_executable(sample_image_mem ${CPP_DIR}/sample/sample_image_mem.cpp ${INSPIRECV_SRC_FILES})
    target_link_libraries(sample_image_mem ${NEED_LINK_LIBS} ${EXTENSION_LIBS})
    add_executable(sample_gauss_blur ${CPP_DIR}/sample/sample_gauss_blur.cpp ${INSPIRECV_SRC_FILES})
    target_link_libraries(sample_gauss_blur ${NEED_LINK_LIBS} ${EXTENSION_LIBS})
    add_executable(sample_morphology ${CPP_DIR}/sample/sample_morphology.cpp ${INSPIRECV_SRC_FILES})
    target_link_libraries(sample_morphology ${NEED_LINK_LIBS} ${EXTENSION_LIBS})
    add_executable(sample_pipeline ${CPP_DIR}/sample/sample_pipeline.cpp ${INSPIRECV_SRC_FILES})
    target_link_libraries(sample_pipeline ${NEED_LINK_LIBS} ${EXTENSION_LIBS})
endif()

if (INSPIRECV_BUILD_TESTS)
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/Catch2/single_include")
    file(GLOB_RECURSE TEST_CASE_FILES "${CPP_DIR}/test/cases/*.cpp")
    add_executable("testing" ${CPP_DIR}/test/test.cpp ${TEST_CASE_FILES} ${INSPIRECV_SRC_FILES} )
    target_link_libraries("testing" ${NEED_LINK_LIBS} ${EXTENSION_LIBS})
endif()

if(INSPIRECV_BUILD_OBJECT_LIBS)
    add_library(inspirecv OBJECT ${INSPIRECV_SRC_FILES})
else()
    if(INSPIRECV_BUILD_SHARED_LIBS)
        add_library(inspirecv SHARED ${INSPIRECV_SRC_FILES})
    else()
        add_library(inspirecv STATIC ${INSPIRECV_SRC_FILES})
    endif()
endif()

target_link_libraries(inspirecv ${NEED_LINK_LIBS} ${EXTENSION_LIBS})

message(STATUS "================================")
message(STATUS "Version: ${INSPIRECV_VERSION_STRING}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "InspireCV Backend: ${INSPIRECV_BACKEND}")

if(INSPIRECV_BACKEND STREQUAL "OpenCV" OR INSPIRECV_BACKEND_OKCV_USE_OPENCV)
    message(STATUS "OpenCV Found")
    message(STATUS "OpenCV Version: ${OpenCV_VERSION}")
    message(STATUS "OpenCV Path: ${OpenCV_DIR}")
endif()

if(INSPIRECV_BACKEND STREQUAL "OKCV")
    message(STATUS "OKCV Found")
endif()

if(INSPIRECV_BACKEND STREQUAL "OKCV")
    message(STATUS "Use OpenCV: ${INSPIRECV_BACKEND_OKCV_USE_OPENCV}")
    message(STATUS "Use OpenCV IO: ${INSPIRECV_BACKEND_OKCV_USE_OPENCV_IO}")
    message(STATUS "Use OpenCV GUI: ${INSPIRECV_BACKEND_OKCV_USE_OPENCV_GUI}")
    if(INSPIRECV_BACKEND_OKCV_USE_OPENCV)
        message(STATUS "OpenCV Version: ${OpenCV_VERSION}")
        message(STATUS "OpenCV Path: ${OpenCV_DIR}")
    endif()
    
endif()
message(STATUS "================================")

# Install 

set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install")
install(TARGETS inspirecv DESTINATION lib)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/inspirecv DESTINATION include)


if(INSPIRECV_BUILD_TESTS)
    install(TARGETS "testing" RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
endif()
