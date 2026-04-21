# Encryption/decryption module used by both client and server
target_sources(hailo_common PRIVATE
    ICipher.h
    AriaCipher.cpp
    AriaCipher.h
)
