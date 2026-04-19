# client 역할: RTSP 수신
target_sources(hailo_common PRIVATE
    RtspClient.cpp
    RtspClient.h
    TlsHandshake.cpp
    TlsHandshake.h
)

# server 역할: RTSP 송출
target_sources(hailo_server PRIVATE
    RtspServer.cpp
    RtspServer.h
    TlsHandshake.cpp
    TlsHandshake.h
)
