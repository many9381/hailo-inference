# 진입점은 양쪽 실행 파일에 동일하게 컴파일된다.
target_sources(hailo_client PRIVATE main.cpp)

# server 전용: Hailo 추론 관련 코드
target_sources(hailo_server PRIVATE
    main.cpp
    Preprocessor.cpp
    Preprocessor.h
    PostProcessor.cpp
    PostProcessor.h
    Visualizer.cpp
    Visualizer.h
    HailoInference.cpp
    HailoInference.h
    YoloTypes.h
)

add_subdirectory(gui)
add_subdirectory(gstreamer)
add_subdirectory(rtsp_native)
