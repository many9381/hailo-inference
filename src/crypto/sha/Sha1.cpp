#include "Sha1.h"

#include <algorithm>
#include <cstring>

// ============================================================================
// SHA-1 내부 유틸리티
// ============================================================================
namespace {

inline uint32_t rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

// Big-endian 변환
inline uint32_t readBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
           (static_cast<uint32_t>(p[3]));
}

inline void writeBE32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >>  8);
    p[3] = static_cast<uint8_t>(v);
}

inline void writeBE64(uint8_t* p, uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        p[i] = static_cast<uint8_t>(v & 0xFF);
        v >>= 8;
    }
}

// SHA-1 라운드 함수
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline uint32_t parity(uint32_t x, uint32_t y, uint32_t z) {
    return x ^ y ^ z;
}

inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

// SHA-1 라운드 상수
static constexpr uint32_t K[4] = {
    0x5A827999,  // 0–19
    0x6ED9EBA1,  // 20–39
    0x8F1BBCDC,  // 40–59
    0xCA62C1D6   // 60–79
};

} // namespace

// ============================================================================
// Sha1 공개 인터페이스
// ============================================================================

Sha1::Sha1() {
    reset();
}

void Sha1::reset() {
    h_[0] = 0x67452301;
    h_[1] = 0xEFCDAB89;
    h_[2] = 0x98BADCFE;
    h_[3] = 0x10325476;
    h_[4] = 0xC3D2E1F0;

    bufferLen_ = 0;
    totalLen_  = 0;
    finalized_ = false;
    std::memset(buffer_, 0, kBlockSize);
}

void Sha1::update(const uint8_t* data, size_t len) {
    if (finalized_ || len == 0) return;

    totalLen_ += len;
    size_t offset = 0;

    // 버퍼에 잔여 데이터가 있으면 먼저 채운다
    if (bufferLen_ > 0) {
        size_t fill = std::min(len, kBlockSize - bufferLen_);
        std::memcpy(buffer_ + bufferLen_, data, fill);
        bufferLen_ += fill;
        offset += fill;

        if (bufferLen_ == kBlockSize) {
            processBlock(buffer_);
            bufferLen_ = 0;
        }
    }

    // 완전한 블록 단위로 처리
    while (offset + kBlockSize <= len) {
        processBlock(data + offset);
        offset += kBlockSize;
    }

    // 남은 데이터를 버퍼에 저장
    if (offset < len) {
        bufferLen_ = len - offset;
        std::memcpy(buffer_, data + offset, bufferLen_);
    }
}

void Sha1::update(const std::vector<uint8_t>& data) {
    update(data.data(), data.size());
}

Sha1::Digest Sha1::finalize() {
    if (finalized_) {
        Digest d{};
        for (int i = 0; i < 5; ++i)
            writeBE32(d.data() + i * 4, h_[i]);
        return d;
    }

    finalized_ = true;

    // 패딩: 1 비트(0x80) + 0 패딩 + 64 비트 길이
    uint64_t bitLen = totalLen_ * 8;

    buffer_[bufferLen_++] = 0x80;

    // 블록에 길이(8 바이트)를 넣을 공간이 부족하면 현재 블록을 처리
    if (bufferLen_ > 56) {
        std::memset(buffer_ + bufferLen_, 0, kBlockSize - bufferLen_);
        processBlock(buffer_);
        bufferLen_ = 0;
    }

    std::memset(buffer_ + bufferLen_, 0, 56 - bufferLen_);
    writeBE64(buffer_ + 56, bitLen);
    processBlock(buffer_);

    Digest d{};
    for (int i = 0; i < 5; ++i)
        writeBE32(d.data() + i * 4, h_[i]);

    return d;
}

Sha1::Digest Sha1::hash(const uint8_t* data, size_t len) {
    Sha1 sha;
    sha.update(data, len);
    return sha.finalize();
}

Sha1::Digest Sha1::hash(const std::vector<uint8_t>& data) {
    return hash(data.data(), data.size());
}

// ============================================================================
// SHA-1 블록 처리 (512 비트 = 64 바이트)
// ============================================================================

void Sha1::processBlock(const uint8_t block[kBlockSize]) {
    uint32_t w[80];

    // 메시지 스케줄: 16 워드 로드 + 64 워드 확장
    for (int i = 0; i < 16; ++i)
        w[i] = readBE32(block + i * 4);

    for (int i = 16; i < 80; ++i)
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = h_[0];
    uint32_t b = h_[1];
    uint32_t c = h_[2];
    uint32_t d = h_[3];
    uint32_t e = h_[4];

    // 80 라운드
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;

        if (i < 20) {
            f = ch(b, c, d);
            k = K[0];
        } else if (i < 40) {
            f = parity(b, c, d);
            k = K[1];
        } else if (i < 60) {
            f = maj(b, c, d);
            k = K[2];
        } else {
            f = parity(b, c, d);
            k = K[3];
        }

        uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl32(b, 30);
        b = a;
        a = temp;
    }

    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
}
