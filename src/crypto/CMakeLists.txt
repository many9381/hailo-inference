# 암복호화 모듈 — client/server 양쪽에서 사용
target_sources(hailo_common PRIVATE
    ICipher.h
    XorCipher.cpp
    XorCipher.h
    AriaCipher.cpp
    AriaCipher.h
    HmacSha1.cpp
    HmacSha1.h
    HmacSha256.cpp
    HmacSha256.h
    Hkdf.cpp
    Hkdf.h
    X25519.cpp
    X25519.h
    SecureRandom.cpp
    SecureRandom.h
    TlsHandshake.cpp
    TlsHandshake.h
)

add_subdirectory(sha)
add_subdirectory(hmac)
