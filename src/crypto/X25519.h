#pragma once

#include <array>
#include <cstdint>

// ----------------------------------------------------------------------------
// X25519
//
// RFC 7748 X25519 Diffie-Hellman 키 교환 구현.
// TLS 1.3 핸드셰이크에서 임시(ephemeral) 키 교환에 사용된다.
//
// 사용법:
//   X25519::KeyPair kp = X25519::generateKeyPair(randomBytes);
//   auto shared = X25519::computeShared(kp.privateKey, peerPublicKey);
//
// 내부 구현은 Curve25519 Montgomery ladder 를 사용하며,
// GF(2^255 - 19) 필드 연산은 5-limb (51 비트) 표현을 사용한다.
// ----------------------------------------------------------------------------
class X25519 {
public:
    static constexpr size_t kKeySize = 32;

    using Key = std::array<uint8_t, kKeySize>;

    struct KeyPair {
        Key privateKey;
        Key publicKey;
    };

    // 32 바이트 랜덤 시드로부터 키 쌍 생성.
    // seed 는 CSPRNG 에서 생성된 32 바이트여야 한다.
    static KeyPair generateKeyPair(const Key& seed);

    // 자신의 비밀 키와 상대방의 공개 키로 공유 비밀을 계산한다.
    // 결과가 all-zero 이면 유효하지 않은 입력이므로 false 를 반환한다.
    static bool computeShared(const Key& myPrivate, const Key& peerPublic,
                              Key& sharedOut);

    // scalar * point 연산 (X25519 함수 본체)
    static Key scalarMult(const Key& scalar, const Key& point);

    // 기저점(basepoint = 9) 을 사용한 scalar multiplication (공개 키 생성)
    static Key scalarMultBase(const Key& scalar);

    // GF(2^255-19) 필드 원소: 5 x uint64_t limb (각 ~51 비트)
    using Fe = uint64_t[5];

private:

    static void feFromBytes(Fe out, const uint8_t in[32]);
    static void feToBytes(uint8_t out[32], const Fe in);

    static void feAdd(Fe out, const Fe a, const Fe b);
    static void feSub(Fe out, const Fe a, const Fe b);
    static void feMul(Fe out, const Fe a, const Fe b);
    static void feSquare(Fe out, const Fe a);
    static void feMul121666(Fe out, const Fe a);
    static void feInvert(Fe out, const Fe a);
    static void feCopy(Fe out, const Fe in);
    static void feReduce(Fe f);
    static void feCswap(Fe a, Fe b, uint64_t swap);
};
