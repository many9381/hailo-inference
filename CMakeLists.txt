cmake_minimum_required(VERSION 3.16)
project(hailo_yolo_inference LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt auto-tooling
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# OpenCV
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui)

# Qt (Qt6 우선, 없으면 Qt5)
find_package(Qt6 COMPONENTS Core Widgets QUIET)
if (NOT Qt6_FOUND)
    find_package(Qt5 REQUIRED COMPONENTS Core Widgets)
    set(QT_LIBS Qt5::Core Qt5::Widgets)
else()
    set(QT_LIBS Qt6::Core Qt6::Widgets)
endif()

# GStreamer (pkg-config)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0
)

# ── server 실행 파일 ──────────────────────────────────────────────────────────
# HailoRT (server 전용 — client 빌드 시에는 탐색하지 않는다)
find_package(HailoRT QUIET)

add_executable(hailo_server)

target_include_directories(hailo_server PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(hailo_server PRIVATE
    ${OpenCV_LIBS}
    ${QT_LIBS}
    PkgConfig::GST
)

if (HailoRT_FOUND)
    target_link_libraries(hailo_server PRIVATE HailoRT::libhailort)
    target_compile_definitions(hailo_server PRIVATE HAS_HAILORT)
else()
    message(WARNING "HailoRT not found — cannot build hailo_server")
endif()

# ── client 실행 파일 ──────────────────────────────────────────────────────────
add_executable(hailo_client)

target_include_directories(hailo_client PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(hailo_client PRIVATE
    ${OpenCV_LIBS}
    ${QT_LIBS}
    PkgConfig::GST
)

add_subdirectory(src)
