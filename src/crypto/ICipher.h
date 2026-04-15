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

    // key 설정 — 알고리즘이 요구하는 크기와 다르면 false 를 반환하고 키를 등록하지 않는다.
    virtual bool setKey(const std::string& key) = 0;
    virtual bool setKey(const std::vector<uint8_t>& key) = 0;

    // IV 설정 — 알고리즘이 요구하는 크기와 다르면 false 를 반환하고 IV 를 등록하지 않는다.
    virtual bool setIv(const std::string& iv) = 0;
    virtual bool setIv(const std::vector<uint8_t>& iv) = 0;

    // in-place 암호화 — 키/IV 가 미설정이면 false 를 반환한다.
    virtual bool encrypt(uint8_t* data, size_t size) const = 0;

    // in-place 복호화 — 키/IV 가 미설정이면 false 를 반환한다.
    virtual bool decrypt(uint8_t* data, size_t size) const = 0;

    // 키가 설정되어 있는지 여부
    virtual bool hasKey() const = 0;
};
