#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

// ----------------------------------------------------------------------------
// SHA3 / SHAKE (FIPS 202)
//
// SHA3-256, SHA3-512 해시 및 SHAKE128, SHAKE256 XOF 구현.
// 내부적으로 Keccak-f[1600] sponge 구조를 사용한다.
//
// 사용법 (SHA3):
//   Sha3_256 sha;
//   sha.update(data, len);
//   auto digest = sha.finalize();
//
//   // 또는 단일 호출:
//   auto digest = Sha3_256::hash(data, len);
//
// 사용법 (SHAKE):
//   Shake128 xof;
//   xof.absorb(data, len);
//   xof.finalize();
//   xof.squeeze(out, outlen);
//
//   // 또는 단일 호출:
//   Shake128::hash(out, outlen, data, len);
// ----------------------------------------------------------------------------

// Keccak sponge 기반 SHA3 공통 구현.
// DigestSize: 출력 해시 바이트 수 (32 또는 64)
// Rate:       sponge rate 바이트 수 (136 또는 72)
template<size_t DigestSize, size_t Rate>
class Sha3Base {
public:
    static constexpr size_t kDigestSize = DigestSize;
    static constexpr size_t kBlockSize  = Rate;  // sponge rate

    using Digest = std::array<uint8_t, kDigestSize>;

    Sha3Base();

    void update(const uint8_t* data, size_t len);
    void update(const std::vector<uint8_t>& data);

    Digest finalize();
    void reset();

    static Digest hash(const uint8_t* data, size_t len);
    static Digest hash(const std::vector<uint8_t>& data);

private:
    uint64_t s_[25]{};
    unsigned int pos_ = 0;
    bool finalized_ = false;
};

// Keccak sponge 기반 SHAKE XOF 공통 구현.
// Rate: sponge rate 바이트 수 (168 또는 136)
template<size_t Rate>
class ShakeBase {
public:
    static constexpr size_t kBlockSize = Rate;  // sponge rate

    ShakeBase();

    // 증분 API
    void absorb(const uint8_t* data, size_t len);
    void absorbOnce(const uint8_t* data, size_t len);
    void finalize();
    void squeeze(uint8_t* out, size_t outlen);
    void reset();

    // 단일 호출 API
    static void hash(uint8_t* out, size_t outlen, const uint8_t* in, size_t inlen);

private:
    uint64_t s_[25]{};
    unsigned int pos_ = 0;
    bool absorbed_ = false;  // finalize 완료 여부
};

// 명시적 인스턴스화 선언
extern template class Sha3Base<32, 136>;
extern template class Sha3Base<64, 72>;
extern template class ShakeBase<168>;
extern template class ShakeBase<136>;

// ── SHA3-256: 32 바이트(256 비트) 다이제스트, rate=136 ─────────────────
using Sha3_256 = Sha3Base<32, 136>;

// ── SHA3-512: 64 바이트(512 비트) 다이제스트, rate=72 ──────────────────
using Sha3_512 = Sha3Base<64, 72>;

// ── SHAKE128: 가변 길이 XOF, rate=168 ─────────────────────────────────
using Shake128 = ShakeBase<168>;

// ── SHAKE256: 가변 길이 XOF, rate=136 ─────────────────────────────────
using Shake256 = ShakeBase<136>;
