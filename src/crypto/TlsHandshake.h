#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ICipher.h"
#include "X25519.h"

// ----------------------------------------------------------------------------
// TlsHandshake
//
// TLS 1.3 핸드셰이크 프로토콜의 키 교환 부분을 구현한다.
// X25519 ECDHE 키 교환과 HKDF-SHA256 키 유도를 수행하여
// SRTP 암복호화 및 RTSP 제어 메시지 암호화에 사용할 키 재료를 생성한다.
//
// 핸드셰이크 프로토콜:
//
//   Client → Server: ClientHello (67 바이트)
//     [msg_type=1 (1B)] [version=0x0304 (2B)] [random (32B)] [pubkey (32B)]
//
//   Server → Client: ServerHello (67 바이트)
//     [msg_type=2 (1B)] [version=0x0304 (2B)] [random (32B)] [pubkey (32B)]
//
//   양측 모두:
//     shared_secret = X25519(my_private, peer_public)
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
    static constexpr uint16_t kTls13Version = 0x0304;
    static constexpr uint8_t  kClientHello  = 1;
    static constexpr uint8_t  kServerHello  = 2;
    static constexpr size_t   kRandomSize   = 32;
    static constexpr size_t   kHandshakeMsgSize = 1 + 2 + 32 + 32;  // 67 바이트

    // 유도된 키 크기
    static constexpr size_t kSrtpKeySize    = 16;  // ARIA-128
    static constexpr size_t kSrtpIvSize     = 16;
    static constexpr size_t kSrtpAuthKeySize = 20; // HMAC-SHA1 키
    static constexpr size_t kRtspKeySize    = 16;  // ARIA-128
    static constexpr size_t kRtspIvSize     = 16;

    TlsHandshake() = default;

    // TCP 소켓 fd 를 통해 서버측 핸드셰이크를 수행한다.
    // ClientHello 수신 → 키 쌍 생성 → ServerHello 송신 → 키 유도
    bool performServerHandshake(int fd);

    // TCP 소켓 fd 를 통해 클라이언트측 핸드셰이크를 수행한다.
    // 키 쌍 생성 → ClientHello 송신 → ServerHello 수신 → 키 유도
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
    // 핸드셰이크 메시지 송수신
    bool sendHandshakeMsg(int fd, uint8_t msgType,
                          const uint8_t random[kRandomSize],
                          const X25519::Key& pubkey);
    bool recvHandshakeMsg(int fd, uint8_t expectedType,
                          uint8_t random[kRandomSize],
                          X25519::Key& pubkey);

    // 공유 비밀로부터 키 재료를 유도한다.
    bool deriveKeys(const X25519::Key& sharedSecret,
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
