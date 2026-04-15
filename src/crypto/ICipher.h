#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// ICipher
//
// RTP payload 암복호화를 위한 인터페이스.
// 구체 알고리즘(XOR, AES 등)은 이 인터페이스를 구현하여 제공한다.
// 사용처에서는 ICipher 포인터만 보유하므로 알고리즘 교체 시
// 생성 지점만 변경하면 된다.
// ----------------------------------------------------------------------------
class ICipher {
public:
    virtual ~ICipher() = default;

    // key 설정
    virtual void setKey(const std::string& key) = 0;
    virtual void setKey(const std::vector<uint8_t>& key) = 0;

    // IV(Initialization Vector) 설정
    virtual void setIv(const std::string& iv) = 0;
    virtual void setIv(const std::vector<uint8_t>& iv) = 0;

    // in-place 암호화
    virtual void encrypt(uint8_t* data, size_t size) const = 0;

    // in-place 복호화
    virtual void decrypt(uint8_t* data, size_t size) const = 0;

    // 키가 설정되어 있는지 여부
    virtual bool hasKey() const = 0;
};
