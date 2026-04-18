#include "TlsHandshake.h"

#include <sys/socket.h>
#include <cstring>

#include "AriaCipher.h"
#include "Hkdf.h"
#include "SecureRandom.h"
#include "X25519.h"

// ============================================================================
// 핸드셰이크 메시지 송수신
// ============================================================================

namespace {

// 지정한 바이트 수만큼 반드시 송신/수신
bool sendAll(int fd, const void* buf, size_t len) {
    const auto* p = static_cast<const uint8_t*>(buf);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, p + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool recvAll(int fd, void* buf, size_t len) {
    auto* p = static_cast<uint8_t*>(buf);
    size_t received = 0;
    while (received < len) {
        ssize_t n = ::recv(fd, p + received, len - received, 0);
        if (n <= 0) return false;
        received += static_cast<size_t>(n);
    }
    return true;
}

} // namespace

bool TlsHandshake::sendHandshakeMsg(int fd, uint8_t msgType,
                                     const uint8_t random[kRandomSize],
                                     const X25519::Key& pubkey) {
    uint8_t msg[kHandshakeMsgSize];
    msg[0] = msgType;
    msg[1] = static_cast<uint8_t>((kTls13Version >> 8) & 0xFF);
    msg[2] = static_cast<uint8_t>(kTls13Version & 0xFF);
    std::memcpy(msg + 3, random, kRandomSize);
    std::memcpy(msg + 3 + kRandomSize, pubkey.data(), X25519::kKeySize);
    return sendAll(fd, msg, kHandshakeMsgSize);
}

bool TlsHandshake::recvHandshakeMsg(int fd, uint8_t expectedType,
                                     uint8_t random[kRandomSize],
                                     X25519::Key& pubkey) {
    uint8_t msg[kHandshakeMsgSize];
    if (!recvAll(fd, msg, kHandshakeMsgSize)) return false;

    // 메시지 타입 검증
    if (msg[0] != expectedType) return false;

    // 버전 검증
    uint16_t version = (static_cast<uint16_t>(msg[1]) << 8) | msg[2];
    if (version != kTls13Version) return false;

    std::memcpy(random, msg + 3, kRandomSize);
    std::memcpy(pubkey.data(), msg + 3 + kRandomSize, X25519::kKeySize);
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

bool TlsHandshake::deriveKeys(const X25519::Key& sharedSecret,
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
// 서버측 핸드셰이크
//
// 1. ClientHello 수신 (client_random + client_pubkey)
// 2. 서버 X25519 키 쌍 생성
// 3. ServerHello 송신 (server_random + server_pubkey)
// 4. 공유 비밀 계산 및 키 유도
// ============================================================================

bool TlsHandshake::performServerHandshake(int fd) {
    // 1. ClientHello 수신
    uint8_t clientRandom[kRandomSize];
    X25519::Key clientPubkey;
    if (!recvHandshakeMsg(fd, kClientHello, clientRandom, clientPubkey))
        return false;

    // 2. 서버 키 쌍 생성
    SystemRandom rng;
    auto seed = rng.generate(X25519::kKeySize);
    if (seed.size() != X25519::kKeySize) return false;

    X25519::Key serverSeed;
    std::memcpy(serverSeed.data(), seed.data(), X25519::kKeySize);
    auto serverKp = X25519::generateKeyPair(serverSeed);

    // 3. ServerHello 송신
    uint8_t serverRandom[kRandomSize];
    if (!rng.generate(serverRandom, kRandomSize)) return false;
    if (!sendHandshakeMsg(fd, kServerHello, serverRandom, serverKp.publicKey))
        return false;

    // 4. 공유 비밀 계산
    X25519::Key sharedSecret;
    if (!X25519::computeShared(serverKp.privateKey, clientPubkey, sharedSecret))
        return false;

    // 5. 키 유도
    return deriveKeys(sharedSecret, clientRandom, serverRandom);
}

// ============================================================================
// 클라이언트측 핸드셰이크
//
// 1. 클라이언트 X25519 키 쌍 생성
// 2. ClientHello 송신 (client_random + client_pubkey)
// 3. ServerHello 수신 (server_random + server_pubkey)
// 4. 공유 비밀 계산 및 키 유도
// ============================================================================

bool TlsHandshake::performClientHandshake(int fd) {
    // 1. 클라이언트 키 쌍 생성
    SystemRandom rng;
    auto seed = rng.generate(X25519::kKeySize);
    if (seed.size() != X25519::kKeySize) return false;

    X25519::Key clientSeed;
    std::memcpy(clientSeed.data(), seed.data(), X25519::kKeySize);
    auto clientKp = X25519::generateKeyPair(clientSeed);

    // 2. ClientHello 송신
    uint8_t clientRandom[kRandomSize];
    if (!rng.generate(clientRandom, kRandomSize)) return false;
    if (!sendHandshakeMsg(fd, kClientHello, clientRandom, clientKp.publicKey))
        return false;

    // 3. ServerHello 수신
    uint8_t serverRandom[kRandomSize];
    X25519::Key serverPubkey;
    if (!recvHandshakeMsg(fd, kServerHello, serverRandom, serverPubkey))
        return false;

    // 4. 공유 비밀 계산
    X25519::Key sharedSecret;
    if (!X25519::computeShared(clientKp.privateKey, serverPubkey, sharedSecret))
        return false;

    // 5. 키 유도
    return deriveKeys(sharedSecret, clientRandom, serverRandom);
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
