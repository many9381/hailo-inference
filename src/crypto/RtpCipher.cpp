#include "RtpCipher.h"

RtpCipher::RtpCipher(const std::string& key)
    : key_(key.begin(), key.end()) {}

RtpCipher::RtpCipher(const std::vector<uint8_t>& key)
    : key_(key) {}

void RtpCipher::encrypt(uint8_t* data, size_t size) const {
    this->xorTransform(data, size);
}

void RtpCipher::decrypt(uint8_t* data, size_t size) const {
    this->xorTransform(data, size);
}

void RtpCipher::xorTransform(uint8_t* data, size_t size) const {
    if (key_.empty() || size == 0) return;

    const size_t keyLen = key_.size();
    for (size_t i = 0; i < size; ++i) {
        data[i] ^= key_[i % keyLen];
    }
}
