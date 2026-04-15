#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// RtpCipher
//
// RTP payload 를 암복호화하는 클래스. XOR 은 대칭 연산이므로 encrypt/decrypt
// 모두 동일한 함수로 처리되지만, 의미 명확성을 위해 별도 이름을 제공한다.
//
// 추후 block cipher(AES 등) 로 교체할 때 이 인터페이스만 변경하면 된다.
// ----------------------------------------------------------------------------
class RtpCipher {
public:
    // key: 암복호화에 사용할 바이트열. 비어있으면 암복호화를 수행하지 않는다.
    explicit RtpCipher(const std::string& key);
    explicit RtpCipher(const std::vector<uint8_t>& key);

    // in-place 암호화 (payload 버퍼를 직접 변조)
    void encrypt(uint8_t* data, size_t size) const;

    // in-place 복호화 (XOR 이므로 encrypt 와 동일하지만 의미를 구분)
    void decrypt(uint8_t* data, size_t size) const;

    bool hasKey() const { return !key_.empty(); }

private:
    void xorTransform(uint8_t* data, size_t size) const;

    std::vector<uint8_t> key_;
};
