#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

// ----------------------------------------------------------------------------
// Sha256
//
// SHA-256 해시 알고리즘 구현 (FIPS 180-4).
// 256 비트(32 바이트) 해시 값을 생성한다.
// TLS 1.3 핸드셰이크의 HKDF-SHA256 에서 사용된다.
//
// 사용법:
//   Sha256 sha;
//   sha.update(data, len);
//   auto digest = sha.finalize();
// ----------------------------------------------------------------------------
class Sha256 {
public:
    static constexpr size_t kDigestSize = 32;   // 256 bit
    static constexpr size_t kBlockSize  = 64;   // 512 bit

    using Digest = std::array<uint8_t, kDigestSize>;

    Sha256();

    void update(const uint8_t* data, size_t len);
    void update(const std::vector<uint8_t>& data);

    Digest finalize();
    void reset();

    static Digest hash(const uint8_t* data, size_t len);
    static Digest hash(const std::vector<uint8_t>& data);

private:
    void processBlock(const uint8_t block[kBlockSize]);

    uint32_t h_[8]{};
    uint8_t  buffer_[kBlockSize]{};
    size_t   bufferLen_ = 0;
    uint64_t totalLen_  = 0;
    bool     finalized_ = false;
};
