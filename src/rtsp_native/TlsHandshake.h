#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <openssl/evp.h>

#include "crypto/ICipher.h"

// ----------------------------------------------------------------------------
// TlsHandshake
//
// ML-KEM-768 (FIPS 203) 기반 핸드셰이크 프로토콜의 키 교환 부분을 구현한다.
// OpenSSL EVP API 를 통한 ML-KEM 키 캡슐화와 HKDF-SHA256 키 유도를 수행하여
// SRTP 암복호화 및 RTSP 제어 메시지 암호화에 사용할 키 재료를 생성한다.
//
// 핸드셰이크 프로토콜 (KEM 방식):
//
//   Server → Client: ServerHello (1219 바이트)
//     [msg_type=2 (1B)] [version=0x0304 (2B)] [random (32B)] [ml_kem_pk (1184B)]
//
//   Client → Server: ClientHello (1123 바이트)
//     [msg_type=1 (1B)] [version=0x0304 (2B)] [random (32B)] [ml_kem_ct (1088B)]
//
//   서버측:
//     pkey = EVP_PKEY_Q_keygen("ML-KEM-768")
//     shared_secret = EVP_PKEY_decapsulate(pkey, ct)
//
//   클라이언트측:
//     (ct, shared_secret) = EVP_PKEY_encapsulate(peer_pk)
//
//   양측 모두:
//     키 유도 = HKDF-SHA256(salt=client_random||server_random, ikm=shared_secret)
//       → srtp_key (16B), srtp_iv (16B), srtp_auth_key (20B), rtsp_key (16B), rtsp_iv (16B)
//
// 사용법 (서버측):
//   TlsHandshake tls;
//   if (!tls.performServerHandshake(fd)) { /* 실패 */ }
//   auto srtpKey = tls.srtpKey();
//
// 사용법 (클라이언트측):
//   TlsHandshake tls;
//   if (!tls.performClientHandshake(fd)) { /* 실패 */ }
//   auto srtpKey = tls.srtpKey();
// ----------------------------------------------------------------------------
class TlsHandshake {
public:
    // ML-KEM-768 (FIPS 203) 파라미터
    static constexpr const char* kMlKemAlg          = "ML-KEM-768";
    static constexpr size_t kMlKemPublicKeyBytes    = 1184;
    static constexpr size_t kMlKemCiphertextBytes   = 1088;
    static constexpr size_t kMlKemSharedSecretBytes = 32;

    static constexpr uint16_t kTls13Version = 0x0304;
    static constexpr uint8_t  kClientHello  = 1;
    static constexpr uint8_t  kServerHello  = 2;
    static constexpr size_t   kRandomSize   = 32;
    static constexpr size_t   kHeaderSize   = 1 + 2 + kRandomSize;  // 35 바이트

    // 유도된 키 크기
    static constexpr size_t kSrtpKeySize    = 16;  // ARIA-128
    static constexpr size_t kSrtpIvSize     = 16;
    static constexpr size_t kSrtpAuthKeySize = 20; // HMAC-SHA1 키
    static constexpr size_t kRtspKeySize    = 16;  // ARIA-128
    static constexpr size_t kRtspIvSize     = 16;

    TlsHandshake() = default;

    // TCP 소켓 fd 를 통해 서버측 핸드셰이크를 수행한다.
    // KEM 키 쌍 생성 → ServerHello 송신(pk) → ClientHello 수신(ct) → 역캡슐화 → 키 유도
    bool performServerHandshake(int fd);

    // TCP 소켓 fd 를 통해 클라이언트측 핸드셰이크를 수행한다.
    // ServerHello 수신(pk) → 캡슐화 → ClientHello 송신(ct) → 키 유도
    bool performClientHandshake(int fd);

    // 유도된 키 접근자
    const std::vector<uint8_t>& srtpKey()     const { return srtpKey_; }
    const std::vector<uint8_t>& srtpIv()      const { return srtpIv_; }
    const std::vector<uint8_t>& srtpAuthKey() const { return srtpAuthKey_; }
    const std::vector<uint8_t>& rtspKey()     const { return rtspKey_; }
    const std::vector<uint8_t>& rtspIv()      const { return rtspIv_; }

    // 핸드셰이크 완료 여부
    bool isComplete() const { return complete_; }

    // 유도된 키로 SRTP 용 ICipher 를 생성한다 (AriaCipher + 설정된 key/iv).
    std::unique_ptr<ICipher> createSrtpCipher() const;

    // 유도된 키로 RTSP 제어 메시지 용 ICipher 를 생성한다.
    std::unique_ptr<ICipher> createRtspCipher() const;

private:
    // 소켓 송수신 헬퍼
    static bool sendAll(int fd, const void* buf, size_t len);
    static bool recvAll(int fd, void* buf, size_t len);

    // 핸드셰이크 메시지 송수신 (가변 길이 페이로드)
    static bool sendHandshakeMsg(int fd, uint8_t msgType,
                                 const uint8_t random[kRandomSize],
                                 const uint8_t* payload, size_t payloadLen);
    static bool recvHandshakeMsg(int fd, uint8_t expectedType,
                                 uint8_t random[kRandomSize],
                                 uint8_t* payload, size_t payloadLen);

    // 공유 비밀로부터 키 재료를 유도한다.
    bool deriveKeys(const std::vector<uint8_t>& sharedSecret,
                    const uint8_t clientRandom[kRandomSize],
                    const uint8_t serverRandom[kRandomSize]);

    // 유도된 키
    std::vector<uint8_t> srtpKey_;
    std::vector<uint8_t> srtpIv_;
    std::vector<uint8_t> srtpAuthKey_;
    std::vector<uint8_t> rtspKey_;
    std::vector<uint8_t> rtspIv_;

    bool complete_ = false;
};
