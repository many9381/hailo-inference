# 암복호화 모듈 — client/server 양쪽에서 사용
target_sources(hailo_common PRIVATE
    ICipher.h
    XorCipher.cpp
    XorCipher.h
    AriaCipher.cpp
    AriaCipher.h
    Hkdf.cpp
    Hkdf.h
    SecureRandom.cpp
    SecureRandom.h
)

add_subdirectory(sha)
add_subdirectory(hmac)
add_subdirectory(pqc-kem)
