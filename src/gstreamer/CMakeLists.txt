target_sources(hailo_server PRIVATE
    VideoPipeline.cpp
    VideoPipeline.h
    H264Encoder.cpp
    H264Encoder.h
)

target_sources(hailo_client PRIVATE
    NalDecoderPipeline.cpp
    NalDecoderPipeline.h
)
