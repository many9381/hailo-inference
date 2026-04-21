# The entry point is compiled identically into both executables.
target_sources(hailo_client PRIVATE main.cpp)
target_sources(hailo_common PRIVATE
    gstreamer/GstBootstrap.cpp
    gstreamer/GstBootstrap.h
)

# Server-only: Hailo inference-related code
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

add_subdirectory(crypto)
add_subdirectory(gui)
add_subdirectory(gstreamer)
add_subdirectory(rtsp_native)
