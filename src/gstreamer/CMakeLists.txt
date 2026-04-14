# client 역할(및 공통)에서 사용
target_sources(hailo_common PRIVATE
    NalDecoderPipeline.cpp
    NalDecoderPipeline.h
)

# server 전용 (VideoPipeline = 파일→프레임, H264Encoder = 프레임→NAL)
target_sources(hailo_server PRIVATE
    VideoPipeline.cpp
    VideoPipeline.h
    H264Encoder.cpp
    H264Encoder.h
)
