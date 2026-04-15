#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ICipher.h"

// ----------------------------------------------------------------------------
// XorCipher
//
// ICipher 의 XOR 구현체. XOR 은 대칭 연산이므로 encrypt/decrypt 모두 동일한
// 함수로 처리되지만, 의미 명확성을 위해 별도 이름을 제공한다.
// IV 는 XOR 에서는 key 와 결합되어 사용된다.
//
// 추후 AES 등 block cipher 로 교체할 때는 ICipher 를 구현하는 새 클래스를
// 작성하고 생성 지점만 변경하면 된다.
// ----------------------------------------------------------------------------
class XorCipher : public ICipher {
public:
    XorCipher() = default;
    explicit XorCipher(const std::string& key);
    explicit XorCipher(const std::vector<uint8_t>& key);

    void setKey(const std::string& key) override;
    void setKey(const std::vector<uint8_t>& key) override;

    void setIv(const std::string& iv) override;
    void setIv(const std::vector<uint8_t>& iv) override;

    void encrypt(uint8_t* data, size_t size) const override;
    void decrypt(uint8_t* data, size_t size) const override;

    bool hasKey() const override { return !key_.empty(); }

private:
    void xorTransform(uint8_t* data, size_t size) const;

    std::vector<uint8_t> key_;
    std::vector<uint8_t> iv_;
};
