#include "XorCipher.h"

bool XorCipher::setKey(const std::string& key) {
    if (key.empty()) return false;
    key_.assign(key.begin(), key.end());
    return true;
}

bool XorCipher::setKey(const std::vector<uint8_t>& key) {
    if (key.empty()) return false;
    key_ = key;
    return true;
}

bool XorCipher::setIv(const std::string& iv) {
    if (iv.empty()) return false;
    iv_.assign(iv.begin(), iv.end());
    return true;
}

bool XorCipher::setIv(const std::vector<uint8_t>& iv) {
    if (iv.empty()) return false;
    iv_ = iv;
    return true;
}

bool XorCipher::encrypt(uint8_t* data, size_t size) const {
    if (key_.empty()) return false;
    this->xorTransform(data, size);
    return true;
}

bool XorCipher::decrypt(uint8_t* data, size_t size) const {
    if (key_.empty()) return false;
    this->xorTransform(data, size);
    return true;
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
