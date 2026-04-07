cmake_minimum_required(VERSION 3.16)
project(hailo_yolo_inference LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt auto-tooling
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# HailoRT
find_package(HailoRT REQUIRED)

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

# 실행 파일 타깃을 먼저 만들고, 소스는 서브디렉토리에서 target_sources로 등록한다.
add_executable(hailo_yolo_inference)

target_include_directories(hailo_yolo_inference PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(hailo_yolo_inference PRIVATE
    HailoRT::libhailort
    ${OpenCV_LIBS}
    ${QT_LIBS}
    PkgConfig::GST
)

add_subdirectory(src)
