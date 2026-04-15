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

# HailoRT 는 hailo_server 전용. 미설치 환경에서는 server 빌드를 건너뛴다.
find_package(HailoRT QUIET)

# ── 공통 정적 라이브러리 ──────────────────────────────────────────────────────
# client 역할에 필요한 코드 + 공통 코드만 포함한다 (HailoRT 의존 없음).
# server 전용 코드(ServerWindow, VideoPipeline, H264Encoder, RtspServer,
# HailoInference, Preprocessor/PostProcessor/Visualizer)는 hailo_server
# 실행 파일 타깃에 직접 target_sources 로 등록된다.
add_library(hailo_common STATIC)

target_include_directories(hailo_common PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(hailo_common PUBLIC
    ${OpenCV_LIBS}
    ${QT_LIBS}
    PkgConfig::GST
)

# ── client 실행 파일 ─────────────────────────────────────────────────────────
add_executable(hailo_client)
target_link_libraries(hailo_client PRIVATE hailo_common)

# ── server 실행 파일 (HailoRT 사용) ───────────────────────────────────────────
if (HailoRT_FOUND)
    add_executable(hailo_server)
    target_link_libraries(hailo_server PRIVATE
        hailo_common
        HailoRT::libhailort
    )
else()
    message(WARNING "HailoRT not found — hailo_server will not be built")
    add_executable(hailo_server EXCLUDE_FROM_ALL)
    target_link_libraries(hailo_server PRIVATE hailo_common)
endif()

add_subdirectory(src)
