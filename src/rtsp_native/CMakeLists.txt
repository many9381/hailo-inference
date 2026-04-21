# Client role: RTSP receive
target_sources(hailo_common PRIVATE
    RtspClient.cpp
    RtspClient.h
    MlKemHandshake.cpp
    MlKemHandshake.h
)

# Server role: RTSP transmit
target_sources(hailo_server PRIVATE
    RtspServer.cpp
    RtspServer.h
    MlKemHandshake.cpp
    MlKemHandshake.h
)
