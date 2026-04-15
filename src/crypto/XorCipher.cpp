#include "XorCipher.h"

XorCipher::XorCipher(const std::string& key)
    : key_(key.begin(), key.end()) {}

XorCipher::XorCipher(const std::vector<uint8_t>& key)
    : key_(key) {}

void XorCipher::setKey(const std::string& key) {
    key_.assign(key.begin(), key.end());
}

void XorCipher::setKey(const std::vector<uint8_t>& key) {
    key_ = key;
}

void XorCipher::setIv(const std::string& iv) {
    iv_.assign(iv.begin(), iv.end());
}

void XorCipher::setIv(const std::vector<uint8_t>& iv) {
    iv_ = iv;
}

void XorCipher::encrypt(uint8_t* data, size_t size) const {
    this->xorTransform(data, size);
}

void XorCipher::decrypt(uint8_t* data, size_t size) const {
    this->xorTransform(data, size);
}

void XorCipher::xorTransform(uint8_t* data, size_t size) const {
    if (key_.empty() || size == 0) return;

    const size_t keyLen = key_.size();
    const size_t ivLen  = iv_.size();

    for (size_t i = 0; i < size; ++i) {
        uint8_t k = key_[i % keyLen];
        if (ivLen > 0) k ^= iv_[i % ivLen];
        data[i] ^= k;
    }
}
