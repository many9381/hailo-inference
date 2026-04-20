#include "TlsHandshake.h"

#include <sys/socket.h>
#include <cstring>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>

#include "crypto/AriaCipher.h"

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

    // info = 고정 라벨
    const char* label = "hailo-tls13-srtp-keys";

    // 총 필요한 키 재료: 16 + 16 + 20 + 16 + 16 = 84 바이트
    constexpr size_t totalKeyLen = kSrtpKeySize + kSrtpIvSize + kSrtpAuthKeySize
                                 + kRtspKeySize + kRtspIvSize;

    // OpenSSL HKDF-SHA256
    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    if (!kdf) return false;

    EVP_KDF_CTX* kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kctx) return false;

    OSSL_PARAM params[5];
    params[0] = OSSL_PARAM_construct_utf8_string(
        OSSL_KDF_PARAM_DIGEST, const_cast<char*>("SHA256"), 0);
    params[1] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_SALT, salt.data(), salt.size());
    params[2] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_KEY,
        const_cast<uint8_t*>(sharedSecret.data()), sharedSecret.size());
    params[3] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_INFO, const_cast<char*>(label), std::strlen(label));
    params[4] = OSSL_PARAM_construct_end();

    std::vector<uint8_t> okm(totalKeyLen);
    int rc = EVP_KDF_derive(kctx, okm.data(), totalKeyLen, params);
    EVP_KDF_CTX_free(kctx);
    if (rc != 1) return false;

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
// 서버측 핸드셰이크 (ML-KEM-768)
//
// 1. ML-KEM 키 쌍 생성 (OpenSSL EVP)
// 2. ServerHello 송신 (server_random + ml_kem_pk)
// 3. ClientHello 수신 (client_random + ml_kem_ct)
// 4. 역캡슐화로 공유 비밀 복원
// 5. 키 유도
// ============================================================================

bool TlsHandshake::performServerHandshake(int fd) {
    // 1. ML-KEM-768 키 쌍 생성
    EVP_PKEY* pkey = EVP_PKEY_Q_keygen(nullptr, nullptr, kMlKemAlg);
    if (!pkey) return false;

    // 공개 키 추출
    std::vector<uint8_t> pk(kMlKemPublicKeyBytes);
    size_t pkLen = kMlKemPublicKeyBytes;
    if (EVP_PKEY_get_raw_public_key(pkey, pk.data(), &pkLen) != 1
        || pkLen != kMlKemPublicKeyBytes) {
        EVP_PKEY_free(pkey);
        return false;
    }

    // 2. ServerHello 송신 (server_random + pk)
    uint8_t serverRandom[kRandomSize];
    if (RAND_bytes(serverRandom, kRandomSize) != 1) {
        EVP_PKEY_free(pkey);
        return false;
    }
    if (!sendHandshakeMsg(fd, kServerHello, serverRandom,
                          pk.data(), kMlKemPublicKeyBytes)) {
        EVP_PKEY_free(pkey);
        return false;
    }

    // 3. ClientHello 수신 (client_random + ct)
    uint8_t clientRandom[kRandomSize];
    std::vector<uint8_t> ct(kMlKemCiphertextBytes);
    if (!recvHandshakeMsg(fd, kClientHello, clientRandom,
                          ct.data(), kMlKemCiphertextBytes)) {
        EVP_PKEY_free(pkey);
        return false;
    }

    // 4. 역캡슐화 → 공유 비밀
    EVP_PKEY_CTX* dctx = EVP_PKEY_CTX_new_from_pkey(nullptr, pkey, nullptr);
    EVP_PKEY_free(pkey);
    if (!dctx) return false;

    if (EVP_PKEY_decapsulate_init(dctx, nullptr) != 1) {
        EVP_PKEY_CTX_free(dctx);
        return false;
    }

    size_t ssLen = 0;
    if (EVP_PKEY_decapsulate(dctx, nullptr, &ssLen,
                             ct.data(), kMlKemCiphertextBytes) != 1) {
        EVP_PKEY_CTX_free(dctx);
        return false;
    }

    std::vector<uint8_t> sharedSecret(ssLen);
    if (EVP_PKEY_decapsulate(dctx, sharedSecret.data(), &ssLen,
                             ct.data(), kMlKemCiphertextBytes) != 1) {
        EVP_PKEY_CTX_free(dctx);
        return false;
    }
    EVP_PKEY_CTX_free(dctx);

    // 5. 키 유도
    return deriveKeys(sharedSecret, clientRandom, serverRandom);
}

// ============================================================================
// 클라이언트측 핸드셰이크 (ML-KEM-768)
//
// 1. ServerHello 수신 (server_random + ml_kem_pk)
// 2. 캡슐화 → (암호문, 공유 비밀)
// 3. ClientHello 송신 (client_random + ml_kem_ct)
// 4. 키 유도
// ============================================================================

bool TlsHandshake::performClientHandshake(int fd) {
    // 1. ServerHello 수신 (server_random + pk)
    uint8_t serverRandom[kRandomSize];
    std::vector<uint8_t> pk(kMlKemPublicKeyBytes);
    if (!recvHandshakeMsg(fd, kServerHello, serverRandom,
                          pk.data(), kMlKemPublicKeyBytes))
        return false;

    // 2. 수신한 공개 키로 EVP_PKEY 복원
    EVP_PKEY* peer = EVP_PKEY_new_raw_public_key_ex(
        nullptr, kMlKemAlg, nullptr, pk.data(), kMlKemPublicKeyBytes);
    if (!peer) return false;

    // 캡슐화 → (암호문, 공유 비밀)
    EVP_PKEY_CTX* ectx = EVP_PKEY_CTX_new_from_pkey(nullptr, peer, nullptr);
    EVP_PKEY_free(peer);
    if (!ectx) return false;

    if (EVP_PKEY_encapsulate_init(ectx, nullptr) != 1) {
        EVP_PKEY_CTX_free(ectx);
        return false;
    }

    size_t ctLen = 0, ssLen = 0;
    if (EVP_PKEY_encapsulate(ectx, nullptr, &ctLen, nullptr, &ssLen) != 1) {
        EVP_PKEY_CTX_free(ectx);
        return false;
    }

    std::vector<uint8_t> ct(ctLen);
    std::vector<uint8_t> sharedSecret(ssLen);
    if (EVP_PKEY_encapsulate(ectx, ct.data(), &ctLen,
                             sharedSecret.data(), &ssLen) != 1) {
        EVP_PKEY_CTX_free(ectx);
        return false;
    }
    EVP_PKEY_CTX_free(ectx);

    // 3. ClientHello 송신 (client_random + ct)
    uint8_t clientRandom[kRandomSize];
    if (RAND_bytes(clientRandom, kRandomSize) != 1) return false;
    if (!sendHandshakeMsg(fd, kClientHello, clientRandom,
                          ct.data(), kMlKemCiphertextBytes))
        return false;

    // 4. 키 유도
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
