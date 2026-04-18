#pragma once

#include "hmac/Hmac.h"
#include "sha/Sha256.h"

// ----------------------------------------------------------------------------
// HmacSha256
//
// HMAC-SHA256 구현. Hmac<Sha256> 을 래핑하여 편의 인터페이스를 제공한다.
// TLS 1.3 핸드셰이크의 HKDF 에서 사용된다.
// ----------------------------------------------------------------------------
class HmacSha256 {
public:
    static constexpr size_t kDigestSize = Sha256::kDigestSize;  // 32 바이트
    static constexpr size_t kBlockSize  = Sha256::kBlockSize;   // 64 바이트

    using Digest = Sha256::Digest;

    HmacSha256() = default;

    void setKey(const uint8_t* key, size_t keyLen);
    void setKey(const std::vector<uint8_t>& key);

    void update(const uint8_t* data, size_t len);
    void update(const std::vector<uint8_t>& data);

    Digest finalize();
    void reset();

    static Digest compute(const uint8_t* key, size_t keyLen,
                          const uint8_t* data, size_t dataLen);
    static Digest compute(const std::vector<uint8_t>& key,
                          const std::vector<uint8_t>& data);

private:
    Hmac<Sha256> hmac_;
};
