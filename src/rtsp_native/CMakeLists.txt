# RtspServer 는 server 전용, RtspClient 는 client 전용
target_sources(hailo_server PRIVATE
    RtspServer.cpp
    RtspServer.h
)

target_sources(hailo_client PRIVATE
    RtspClient.cpp
    RtspClient.h
)
