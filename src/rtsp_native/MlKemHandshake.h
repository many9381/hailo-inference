#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <openssl/evp.h>

#include "crypto/ICipher.h"

// ----------------------------------------------------------------------------
// MlKemHandshake
//
// Implements the key-exchange portion of a handshake protocol based on ML-KEM-768 (FIPS 203).
// Performs ML-KEM key encapsulation through the OpenSSL EVP API and HKDF-SHA256 key derivation
// to generate key material for SRTP encryption/decryption and RTSP control-message encryption.
//
// Handshake protocol (KEM-based):
//
//   Server -> Client: ServerHello (1219 bytes)
//     [msg_type=2 (1B)] [version=0x0304 (2B)] [random (32B)] [ml_kem_pk (1184B)]
//
//   Client -> Server: ClientHello (1123 bytes)
//     [msg_type=1 (1B)] [version=0x0304 (2B)] [random (32B)] [ml_kem_ct (1088B)]
//
//   Server side:
//     pkey = EVP_PKEY_Q_keygen("ML-KEM-768")
//     shared_secret = EVP_PKEY_decapsulate(pkey, ct)
//
//   Client side:
//     (ct, shared_secret) = EVP_PKEY_encapsulate(peer_pk)
//
//   Both sides:
//     key derivation = HKDF-SHA256(salt=client_random||server_random, ikm=shared_secret)
//       → srtp_key (16B), srtp_iv (16B), srtp_auth_key (20B), rtsp_key (16B), rtsp_iv (16B)
//
// Usage (server side):
//   MlKemHandshake hs;
//   if (!hs.performServerHandshake(fd)) { /* failed */ }
//   auto srtpKey = hs.srtpKey();
//
// Usage (client side):
//   MlKemHandshake hs;
//   if (!hs.performClientHandshake(fd)) { /* failed */ }
//   auto srtpKey = hs.srtpKey();
// ----------------------------------------------------------------------------
class MlKemHandshake {
public:
    // ML-KEM-768 (FIPS 203) parameters
    static constexpr const char* kMlKemAlg          = "ML-KEM-768";
    static constexpr size_t kMlKemPublicKeyBytes    = 1184;
    static constexpr size_t kMlKemCiphertextBytes   = 1088;
    static constexpr size_t kMlKemSharedSecretBytes = 32;

    static constexpr uint16_t kHandshakeVersion = 0x0304;
    static constexpr uint8_t  kClientHello  = 1;
    static constexpr uint8_t  kServerHello  = 2;
    static constexpr size_t   kRandomSize   = 32;
    static constexpr size_t   kHeaderSize   = 1 + 2 + kRandomSize;  // 35 bytes

    // Derived key sizes
    static constexpr size_t kSrtpKeySize    = 16;  // ARIA-128
    static constexpr size_t kSrtpIvSize     = 16;
    static constexpr size_t kSrtpAuthKeySize = 20; // HMAC-SHA1 key
    static constexpr size_t kRtspKeySize    = 16;  // ARIA-128
    static constexpr size_t kRtspIvSize     = 16;

    MlKemHandshake() = default;

    // Perform the server-side handshake over the TCP socket fd.
    // Generate KEM key pair -> send ServerHello (pk) -> receive ClientHello (ct) -> decapsulate -> derive keys
    bool performServerHandshake(int fd);

    // Perform the client-side handshake over the TCP socket fd.
    // Receive ServerHello (pk) -> encapsulate -> send ClientHello (ct) -> derive keys
    bool performClientHandshake(int fd);

    // Derived key accessors
    const std::vector<uint8_t>& srtpKey()     const { return srtpKey_; }
    const std::vector<uint8_t>& srtpIv()      const { return srtpIv_; }
    const std::vector<uint8_t>& srtpAuthKey() const { return srtpAuthKey_; }
    const std::vector<uint8_t>& rtspKey()     const { return rtspKey_; }
    const std::vector<uint8_t>& rtspIv()      const { return rtspIv_; }

    // Whether the handshake has completed
    bool isComplete() const { return complete_; }

    // Create an ICipher for SRTP from the derived keys (AriaCipher + configured key/iv).
    std::unique_ptr<ICipher> createSrtpCipher() const;

    // Create an ICipher for RTSP control messages from the derived keys.
    std::unique_ptr<ICipher> createRtspCipher() const;

private:
    // Socket send/receive helpers
    static bool sendAll(int fd, const void* buf, size_t len);
    static bool recvAll(int fd, void* buf, size_t len);

    // Handshake message send/receive helpers (variable-length payload)
    static bool sendHandshakeMsg(int fd, uint8_t msgType,
                                 const uint8_t random[kRandomSize],
                                 const uint8_t* payload, size_t payloadLen);
    static bool recvHandshakeMsg(int fd, uint8_t expectedType,
                                 uint8_t random[kRandomSize],
                                 uint8_t* payload, size_t payloadLen);

    // Derive key material from the shared secret.
    bool deriveKeys(const std::vector<uint8_t>& sharedSecret,
                    const uint8_t clientRandom[kRandomSize],
                    const uint8_t serverRandom[kRandomSize]);

    // Derived keys
    std::vector<uint8_t> srtpKey_;
    std::vector<uint8_t> srtpIv_;
    std::vector<uint8_t> srtpAuthKey_;
    std::vector<uint8_t> rtspKey_;
    std::vector<uint8_t> rtspIv_;

    bool complete_ = false;
};
