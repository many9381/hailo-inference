# 암복호화 모듈 — client/server 양쪽에서 사용
target_sources(hailo_common PRIVATE
    ICipher.h
    XorCipher.cpp
    XorCipher.h
    AriaCipher.cpp
    AriaCipher.h
    HmacSha1.cpp
    HmacSha1.h
)

add_subdirectory(sha)
add_subdirectory(hmac)
