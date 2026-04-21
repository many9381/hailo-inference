# Used by the client role (and shared code)
target_sources(hailo_common PRIVATE
    NalDecoderPipeline.cpp
    NalDecoderPipeline.h
)

# Server-only (VideoPipeline = file->frame, H264Encoder = frame->NAL)
target_sources(hailo_server PRIVATE
    VideoPipeline.cpp
    VideoPipeline.h
    H264Encoder.cpp
    H264Encoder.h
)
