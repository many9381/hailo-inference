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
