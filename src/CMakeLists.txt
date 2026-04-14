# ── server 소스 ───────────────────────────────────────────────────────────────
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
target_compile_definitions(hailo_server PRIVATE BUILD_SERVER)

# ── client 소스 ───────────────────────────────────────────────────────────────
target_sources(hailo_client PRIVATE
    main.cpp
)
target_compile_definitions(hailo_client PRIVATE BUILD_CLIENT)

add_subdirectory(gui)
add_subdirectory(gstreamer)
add_subdirectory(rtsp_native)
add_subdirectory(client)
