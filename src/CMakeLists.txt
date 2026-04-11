# src/ 직속 소스 파일 등록
target_sources(hailo_yolo_inference PRIVATE
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
# src/rtsp/ 는 GStreamer 기반 구현이며 빌드에서 제외한다. 소스는 그대로 보존.
# add_subdirectory(rtsp)
add_subdirectory(rtsp_native)
