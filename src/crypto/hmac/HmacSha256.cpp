#include "HmacSha256.h"

void HmacSha256::setKey(const uint8_t* key, size_t keyLen) {
    hmac_.setKey(key, keyLen);
}

void HmacSha256::setKey(const std::vector<uint8_t>& key) {
    hmac_.setKey(key);
}

void HmacSha256::update(const uint8_t* data, size_t len) {
    hmac_.update(data, len);
}

void HmacSha256::update(const std::vector<uint8_t>& data) {
    hmac_.update(data);
}

HmacSha256::Digest HmacSha256::finalize() {
    return hmac_.finalize();
}

void HmacSha256::reset() {
    hmac_.reset();
}

HmacSha256::Digest HmacSha256::compute(
    const uint8_t* key, size_t keyLen,
    const uint8_t* data, size_t dataLen) {
    return Hmac<Sha256>::compute(key, keyLen, data, dataLen);
}

HmacSha256::Digest HmacSha256::compute(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& data) {
    return Hmac<Sha256>::compute(key, data);
}
