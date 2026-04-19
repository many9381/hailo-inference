#include "TlsHandshake.h"

#include <sys/socket.h>
#include <cstring>

#include "crypto/AriaCipher.h"
#include "crypto/Hkdf.h"
#include "crypto/SecureRandom.h"

// ============================================================================
// 소켓 송수신 헬퍼
// ============================================================================

bool TlsHandshake::sendAll(int fd, const void* buf, size_t len) {
    const auto* p = static_cast<const uint8_t*>(buf);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, p + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool TlsHandshake::recvAll(int fd, void* buf, size_t len) {
    auto* p = static_cast<uint8_t*>(buf);
    size_t received = 0;
    while (received < len) {
        ssize_t n = ::recv(fd, p + received, len - received, 0);
        if (n <= 0) return false;
        received += static_cast<size_t>(n);
    }
    return true;
}

// ============================================================================
// 핸드셰이크 메시지 송수신
// ============================================================================

bool TlsHandshake::sendHandshakeMsg(int fd, uint8_t msgType,
                                     const uint8_t random[kRandomSize],
                                     const uint8_t* payload, size_t payloadLen) {
    const size_t msgSize = kHeaderSize + payloadLen;
    std::vector<uint8_t> msg(msgSize);
    msg[0] = msgType;
    msg[1] = static_cast<uint8_t>((kTls13Version >> 8) & 0xFF);
    msg[2] = static_cast<uint8_t>(kTls13Version & 0xFF);
    std::memcpy(msg.data() + 3, random, kRandomSize);
    std::memcpy(msg.data() + kHeaderSize, payload, payloadLen);
    return sendAll(fd, msg.data(), msgSize);
}

bool TlsHandshake::recvHandshakeMsg(int fd, uint8_t expectedType,
                                     uint8_t random[kRandomSize],
                                     uint8_t* payload, size_t payloadLen) {
    const size_t msgSize = kHeaderSize + payloadLen;
    std::vector<uint8_t> msg(msgSize);
    if (!recvAll(fd, msg.data(), msgSize)) return false;

    // 메시지 타입 검증
    if (msg[0] != expectedType) return false;

    // 버전 검증
    uint16_t version = (static_cast<uint16_t>(msg[1]) << 8) | msg[2];
    if (version != kTls13Version) return false;

    std::memcpy(random, msg.data() + 3, kRandomSize);
    std::memcpy(payload, msg.data() + kHeaderSize, payloadLen);
    return true;
}

// ============================================================================
// 키 유도 (HKDF-SHA256)
//
// salt    = client_random || server_random  (64 바이트)
// ikm     = shared_secret                  (32 바이트)
// info    = "hailo-tls13-srtp-keys"
// output  = srtp_key(16) + srtp_iv(16) + srtp_auth_key(20) + rtsp_key(16) + rtsp_iv(16) = 84 바이트
// ============================================================================

bool TlsHandshake::deriveKeys(const std::vector<uint8_t>& sharedSecret,
                               const uint8_t clientRandom[kRandomSize],
                               const uint8_t serverRandom[kRandomSize]) {
    // salt = client_random || server_random
    std::vector<uint8_t> salt(kRandomSize * 2);
    std::memcpy(salt.data(), clientRandom, kRandomSize);
    std::memcpy(salt.data() + kRandomSize, serverRandom, kRandomSize);

    // ikm = shared_secret
    std::vector<uint8_t> ikm(sharedSecret.begin(), sharedSecret.end());

    // info = 고정 라벨
    const std::string label = "hailo-tls13-srtp-keys";
    std::vector<uint8_t> info(label.begin(), label.end());

    // 총 필요한 키 재료: 16 + 16 + 20 + 16 + 16 = 84 바이트
    const size_t totalKeyLen = kSrtpKeySize + kSrtpIvSize + kSrtpAuthKeySize
                             + kRtspKeySize + kRtspIvSize;

    auto okm = Hkdf::deriveKey(salt, ikm, info, totalKeyLen);
    if (okm.size() != totalKeyLen) return false;

    // 키 재료를 분배
    size_t offset = 0;

    srtpKey_.assign(okm.begin() + offset, okm.begin() + offset + kSrtpKeySize);
    offset += kSrtpKeySize;

    srtpIv_.assign(okm.begin() + offset, okm.begin() + offset + kSrtpIvSize);
    offset += kSrtpIvSize;

    srtpAuthKey_.assign(okm.begin() + offset, okm.begin() + offset + kSrtpAuthKeySize);
    offset += kSrtpAuthKeySize;

    rtspKey_.assign(okm.begin() + offset, okm.begin() + offset + kRtspKeySize);
    offset += kRtspKeySize;

    rtspIv_.assign(okm.begin() + offset, okm.begin() + offset + kRtspIvSize);

    complete_ = true;
    return true;
}

// ============================================================================
// 서버측 핸드셰이크 (PQC-KEM)
//
// 1. KEM 키 쌍 생성
// 2. ServerHello 송신 (server_random + kem_public_key)
// 3. ClientHello 수신 (client_random + kem_ciphertext)
// 4. 역캡슐화로 공유 비밀 복원
// 5. 키 유도
// ============================================================================

bool TlsHandshake::performServerHandshake(int fd) {
    // 1. KEM 키 쌍 생성
    Kem kem;
    auto kp = kem.keygen();

    // 2. ServerHello 송신 (server_random + pk)
    SystemRandom rng;
    uint8_t serverRandom[kRandomSize];
    if (!rng.generate(serverRandom, kRandomSize)) return false;
    if (!sendHandshakeMsg(fd, kServerHello, serverRandom,
                          kp.publicKey.data(), Kem::PublicKeyBytes))
        return false;

    // 3. ClientHello 수신 (client_random + ct)
    uint8_t clientRandom[kRandomSize];
    std::vector<uint8_t> ct(Kem::CiphertextBytes);
    if (!recvHandshakeMsg(fd, kClientHello, clientRandom,
                          ct.data(), Kem::CiphertextBytes))
        return false;

    // 4. 역캡슐화 → 공유 비밀
    auto sharedSecret = kem.decapsulate(ct.data(), kp.secretKey.data());

    // 5. 키 유도
    return deriveKeys(sharedSecret, clientRandom, serverRandom);
}

// ============================================================================
// 클라이언트측 핸드셰이크 (PQC-KEM)
//
// 1. ServerHello 수신 (server_random + kem_public_key)
// 2. 캡슐화 → (암호문, 공유 비밀)
// 3. ClientHello 송신 (client_random + kem_ciphertext)
// 4. 키 유도
// ============================================================================

bool TlsHandshake::performClientHandshake(int fd) {
    // 1. ServerHello 수신 (server_random + pk)
    uint8_t serverRandom[kRandomSize];
    std::vector<uint8_t> pk(Kem::PublicKeyBytes);
    if (!recvHandshakeMsg(fd, kServerHello, serverRandom,
                          pk.data(), Kem::PublicKeyBytes))
        return false;

    // 2. 캡슐화 → (암호문, 공유 비밀)
    Kem kem;
    auto result = kem.encapsulate(pk.data());

    // 3. ClientHello 송신 (client_random + ct)
    SystemRandom rng;
    uint8_t clientRandom[kRandomSize];
    if (!rng.generate(clientRandom, kRandomSize)) return false;
    if (!sendHandshakeMsg(fd, kClientHello, clientRandom,
                          result.ciphertext.data(), Kem::CiphertextBytes))
        return false;

    // 4. 키 유도
    return deriveKeys(result.sharedSecret, clientRandom, serverRandom);
}

// ============================================================================
// Cipher 팩토리
// ============================================================================

std::unique_ptr<ICipher> TlsHandshake::createSrtpCipher() const {
    if (!complete_) return nullptr;
    auto c = std::make_unique<AriaCipher>();
    c->setKey(srtpKey_);
    c->setIv(srtpIv_);
    return c;
}

std::unique_ptr<ICipher> TlsHandshake::createRtspCipher() const {
    if (!complete_) return nullptr;
    auto c = std::make_unique<AriaCipher>();
    c->setKey(rtspKey_);
    c->setIv(rtspIv_);
    return c;
}
