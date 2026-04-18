#include "Sha256.h"

#include <cstring>

// ============================================================================
// SHA-256 상수 및 유틸리티
// ============================================================================
namespace {

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t sigma0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
inline uint32_t sigma1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
inline uint32_t gamma0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
inline uint32_t gamma1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

inline uint32_t readBe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

inline void writeBe32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

} // namespace

// ============================================================================
// Sha256 구현
// ============================================================================

Sha256::Sha256() { reset(); }

void Sha256::reset() {
    h_[0] = 0x6a09e667;
    h_[1] = 0xbb67ae85;
    h_[2] = 0x3c6ef372;
    h_[3] = 0xa54ff53a;
    h_[4] = 0x510e527f;
    h_[5] = 0x9b05688c;
    h_[6] = 0x1f83d9ab;
    h_[7] = 0x5be0cd19;
    bufferLen_ = 0;
    totalLen_  = 0;
    finalized_ = false;
}

void Sha256::processBlock(const uint8_t block[kBlockSize]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = readBe32(block + i * 4);
    for (int i = 16; i < 64; ++i)
        w[i] = gamma1(w[i-2]) + w[i-7] + gamma0(w[i-15]) + w[i-16];

    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + sigma1(e) + ch(e, f, g) + K[i] + w[i];
        uint32_t t2 = sigma0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}

void Sha256::update(const uint8_t* data, size_t len) {
    if (finalized_ || len == 0) return;
    totalLen_ += len;

    // 잔여 버퍼가 있으면 먼저 채운다
    if (bufferLen_ > 0) {
        size_t fill = kBlockSize - bufferLen_;
        if (len < fill) {
            std::memcpy(buffer_ + bufferLen_, data, len);
            bufferLen_ += len;
            return;
        }
        std::memcpy(buffer_ + bufferLen_, data, fill);
        processBlock(buffer_);
        data += fill;
        len  -= fill;
        bufferLen_ = 0;
    }

    while (len >= kBlockSize) {
        processBlock(data);
        data += kBlockSize;
        len  -= kBlockSize;
    }

    if (len > 0) {
        std::memcpy(buffer_, data, len);
        bufferLen_ = len;
    }
}

void Sha256::update(const std::vector<uint8_t>& data) {
    update(data.data(), data.size());
}

Sha256::Digest Sha256::finalize() {
    if (finalized_) return Digest{};
    finalized_ = true;

    uint64_t totalBits = totalLen_ * 8;

    // 패딩: 0x80 + 0 패딩 + 8 바이트 길이
    buffer_[bufferLen_++] = 0x80;

    if (bufferLen_ > 56) {
        std::memset(buffer_ + bufferLen_, 0, kBlockSize - bufferLen_);
        processBlock(buffer_);
        bufferLen_ = 0;
    }
    std::memset(buffer_ + bufferLen_, 0, 56 - bufferLen_);

    for (int i = 0; i < 8; ++i)
        buffer_[56 + i] = static_cast<uint8_t>(totalBits >> (56 - i * 8));

    processBlock(buffer_);

    Digest digest{};
    for (int i = 0; i < 8; ++i)
        writeBe32(digest.data() + i * 4, h_[i]);

    return digest;
}

Sha256::Digest Sha256::hash(const uint8_t* data, size_t len) {
    Sha256 ctx;
    ctx.update(data, len);
    return ctx.finalize();
}

Sha256::Digest Sha256::hash(const std::vector<uint8_t>& data) {
    return hash(data.data(), data.size());
}
