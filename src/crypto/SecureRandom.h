#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// ----------------------------------------------------------------------------
// ISecureRandom
//
// 암호학적으로 안전한 난수 생성 인터페이스.
// TLS 핸드셰이크의 nonce 및 PQC-KEM 키 생성에 사용된다.
// ----------------------------------------------------------------------------
class ISecureRandom {
public:
    virtual ~ISecureRandom() = default;

    // buf 에 size 바이트의 암호학적 난수를 채운다.
    // 실패 시 false 를 반환한다.
    virtual bool generate(uint8_t* buf, size_t size) = 0;

    // 편의: 지정 크기의 난수 벡터를 반환한다.
    virtual std::vector<uint8_t> generate(size_t size) = 0;
};

// ----------------------------------------------------------------------------
// SystemRandom
//
// ISecureRandom 의 시스템 구현체.
// macOS/Linux: /dev/urandom 또는 arc4random_buf() 를 사용한다.
// ----------------------------------------------------------------------------
class SystemRandom : public ISecureRandom {
public:
    SystemRandom() = default;

    bool generate(uint8_t* buf, size_t size) override;
    std::vector<uint8_t> generate(size_t size) override;
};
