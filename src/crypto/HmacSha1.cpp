#include "HmacSha1.h"

// ============================================================================
// HmacSha1 공개 인터페이스
// ============================================================================

void HmacSha1::setKey(const uint8_t* key, size_t keyLen) {
    hmac_.setKey(key, keyLen);
}

void HmacSha1::setKey(const std::vector<uint8_t>& key) {
    hmac_.setKey(key);
}

void HmacSha1::update(const uint8_t* data, size_t len) {
    hmac_.update(data, len);
}

void HmacSha1::update(const std::vector<uint8_t>& data) {
    hmac_.update(data);
}

HmacSha1::Digest HmacSha1::finalize() {
    return hmac_.finalize();
}

void HmacSha1::reset() {
    hmac_.reset();
}

HmacSha1::Digest HmacSha1::compute(
    const uint8_t* key, size_t keyLen,
    const uint8_t* data, size_t dataLen) {
    return Hmac<Sha1>::compute(key, keyLen, data, dataLen);
}

HmacSha1::Digest HmacSha1::compute(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& data) {
    return Hmac<Sha1>::compute(key, data);
}
