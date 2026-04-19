#include "SmaugtKem.h"
#include "crypto/sha/Sha3.h"
#include "../SecureRandom.h"

#include <algorithm>
#include <cstring>

// ============================================================================
// 해시 함수 래퍼
// ============================================================================

template<SmaugtMode Mode>
void SmaugtKem<Mode>::hashH(uint8_t out[32], const uint8_t* in, size_t inlen) {
    auto d = Sha3_256::hash(in, inlen);
    std::memcpy(out, d.data(), 32);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::hashG(uint8_t* out, size_t outlen,
                            const uint8_t* in1, size_t inlen1,
                            const uint8_t* in2, size_t inlen2) {
    Shake256 xof;
    xof.absorb(in1, inlen1);
    xof.absorb(in2, inlen2);
    xof.finalize();
    xof.squeeze(out, outlen);
}

// ============================================================================
// 상수 시간 유틸리티
// ============================================================================

template<SmaugtMode Mode>
int SmaugtKem<Mode>::ctVerify(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t r = 0;
    for (size_t i = 0; i < len; i++)
        r |= a[i] ^ b[i];
    return static_cast<int>((-static_cast<uint64_t>(r)) >> 63);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::ctCmov(uint8_t* r, const uint8_t* x, size_t len, uint8_t b) {
    b = static_cast<uint8_t>(-b);
    for (size_t i = 0; i < len; i++)
        r[i] ^= b & (r[i] ^ x[i]);
}

// ============================================================================
// 다항식 산술
// ============================================================================

template<SmaugtMode Mode>
void SmaugtKem<Mode>::polyAdd(Poly* r, const Poly* a, const Poly* b) {
    for (unsigned int i = 0; i < P::N; i++)
        r->coeffs[i] = a->coeffs[i] + b->coeffs[i];
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::polySub(Poly* r, const Poly* a, const Poly* b) {
    for (unsigned int i = 0; i < P::N; i++)
        r->coeffs[i] = a->coeffs[i] - b->coeffs[i];
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::vecVecMult(Poly* r, const PolyVec* a, const PolyVec* b) {
    for (int i = 0; i < P::K; i++)
        SmaugtPoly::polyMulAcc(a->vec[i].coeffs, b->vec[i].coeffs, r->coeffs);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::vecVecMultAdd(Poly* r, const PolyVec* a,
                                    const PolyVec* b, uint8_t mod) {
    PolyVec al;
    Poly res;
    for (int i = 0; i < P::K; ++i)
        for (unsigned int j = 0; j < P::N; ++j)
            al.vec[i].coeffs[j] = a->vec[i].coeffs[j] >> mod;

    std::memset(&res, 0, sizeof(Poly));
    vecVecMult(&res, &al, b);
    for (unsigned int j = 0; j < P::N; ++j)
        res.coeffs[j] <<= mod;

    polyAdd(r, r, &res);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::matVecMultAdd(PolyVec* r, const PolyVec A[P::K],
                                    const PolyVec* b) {
    PolyVec at;
    for (int i = 0; i < P::K; ++i) {
        for (int j = 0; j < P::K; ++j)
            for (unsigned int k = 0; k < P::N; ++k)
                at.vec[j].coeffs[k] = A[j].vec[i].coeffs[k] >> P::Mod16LogQ;

        vecVecMult(&r->vec[i], &at, b);
        for (unsigned int j = 0; j < P::N; ++j)
            r->vec[i].coeffs[j] <<= P::Mod16LogQ;
    }
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::matVecMultSub(PolyVec* r, const PolyVec A[P::K],
                                    const PolyVec* b) {
    PolyVec al;
    Poly res;
    for (int i = 0; i < P::K; ++i) {
        for (int j = 0; j < P::K; ++j)
            for (unsigned int k = 0; k < P::N; ++k)
                al.vec[j].coeffs[k] = A[i].vec[j].coeffs[k] >> P::Mod16LogQ;

        std::memset(&res, 0, sizeof(Poly));
        vecVecMult(&res, &al, b);
        for (unsigned int j = 0; j < P::N; ++j)
            res.coeffs[j] <<= P::Mod16LogQ;

        polySub(&r->vec[i], &r->vec[i], &res);
    }
}

// ============================================================================
// 샘플링
// ============================================================================

namespace {
void load16LE(uint16_t* out, size_t outlen, const uint8_t* in) {
    int pos = 0;
    for (size_t i = 0; i < outlen; ++i) {
        out[i] = static_cast<uint16_t>(in[pos]) |
                 (static_cast<uint16_t>(in[pos + 1]) << 8);
        pos += 2;
    }
}

void load64LE(uint64_t* out, size_t outlen, const uint8_t* in) {
    for (size_t i = 0; i < outlen / 10; ++i) {
        unsigned int pos = static_cast<unsigned int>(i) * 80;
        for (size_t j = 0; j < 10; j++) {
            out[j] = static_cast<uint64_t>(in[pos + j]) |
                     (static_cast<uint64_t>(in[pos + 10 + j]) << 8) |
                     (static_cast<uint64_t>(in[pos + 20 + j]) << 16) |
                     (static_cast<uint64_t>(in[pos + 30 + j]) << 24) |
                     (static_cast<uint64_t>(in[pos + 40 + j]) << 32) |
                     (static_cast<uint64_t>(in[pos + 50 + j]) << 40) |
                     (static_cast<uint64_t>(in[pos + 60 + j]) << 48) |
                     (static_cast<uint64_t>(in[pos + 70 + j]) << 56);
        }
    }
}
} // anonymous namespace

template<SmaugtMode Mode>
int SmaugtKem<Mode>::hwt(int16_t* res, const uint8_t* seed) {
    constexpr size_t hwtSeedBytes = P::HwtSeedBytes;
    int16_t si[P::N] = {};
    uint16_t randBuf[hwtSeedBytes / 2] = {};
    uint8_t sign[P::N / 4] = {};
    uint8_t buf[hwtSeedBytes] = {};

    Shake256 xof;
    xof.absorbOnce(seed, P::CryptoBytes + 2);

    xof.squeeze(buf, hwtSeedBytes);
    load16LE(randBuf, hwtSeedBytes / 2, buf);

    // rej_sample_mod
    unsigned int extraIdx = P::N;
    for (unsigned int i = 0; i < P::N; i++) {
        uint16_t s = static_cast<uint16_t>(P::N - i);
        uint16_t t = 65536 % s;
        uint32_t m = static_cast<uint32_t>(randBuf[i]) * s;
        uint16_t l = static_cast<uint16_t>(m);
        while (l < t) {
            if (extraIdx >= hwtSeedBytes / 2) return -1;
            m = static_cast<uint32_t>(randBuf[extraIdx++]) * s;
            l = static_cast<uint16_t>(m);
        }
        si[i] = static_cast<int16_t>(m >> 16);
    }

    xof.squeeze(sign, P::N / 4);

    int16_t c0 = static_cast<int16_t>(P::N - P::HS);
    for (unsigned int i = 0; i < P::N; i++) {
        int16_t t0 = static_cast<int16_t>((si[i] - c0) >> 15);
        c0 += t0;
        res[i] = static_cast<int16_t>(1 + t0);
        res[i] = static_cast<int16_t>(
            (-res[i]) &
            ((((sign[(((i >> 4) >> 3) << 4) + (i & 0x0F)] >> ((i >> 4) & 0x07)) << 1) & 0x02) - 1));
    }
    return 0;
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::expandS(PolyVec* sk, const uint8_t seed[P::CryptoBytes]) {
    uint8_t extseed[P::CryptoBytes + 2] = {};
    std::memcpy(extseed, seed, P::CryptoBytes);
    for (int i = 0; i < P::K; ++i) {
        extseed[P::CryptoBytes] = static_cast<uint8_t>(i * P::K);
        unsigned int j = 0;
        do {
            extseed[P::CryptoBytes + 1] = static_cast<uint8_t>(j);
            j += 1;
        } while (hwt(sk->vec[i].coeffs, extseed));
    }
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::dGaussianPoly(Poly* op, const uint8_t* seed) {
    uint64_t seedTemp[P::DgSeedLen] = {};
    uint8_t buf[P::DgSeedLen * 8] = {};
    uint64_t s[P::DgSLen] = {};

    Shake256::hash(buf, P::DgSeedLen * 8, seed, P::CryptoBytes + 1);
    load64LE(seedTemp, P::DgSeedLen, buf);

    unsigned int j = 0;
    for (unsigned int i = 0; i < P::N; i += 64) {
        const uint64_t* x = seedTemp + j;
        s[0] = (x[0] & x[1] & x[2] & x[3] & x[4] & x[5] & x[7] & ~x[8]) |
               (x[0] & x[3] & x[4] & x[5] & x[6] & x[8]) |
               (x[1] & x[3] & x[4] & x[5] & x[6] & x[8]) |
               (x[2] & x[3] & x[4] & x[5] & x[6] & x[8]) |
               (~x[2] & ~x[3] & ~x[6] & x[8]) | (~x[1] & ~x[3] & ~x[6] & x[8]) |
               (x[6] & x[7] & ~x[8]) | (~x[5] & ~x[6] & x[8]) |
               (~x[4] & ~x[6] & x[8]) | (~x[7] & x[8]);
        s[1] = (x[1] & x[2] & x[4] & x[5] & x[7] & x[8]) |
               (x[3] & x[4] & x[5] & x[7] & x[8]) | (x[6] & x[7] & x[8]);

        for (unsigned int k = 0; k < 64; ++k) {
            op->coeffs[i + k] =
                static_cast<int16_t>(((s[0] >> k) & 0x01) | (((s[1] >> k) & 0x01) << 1));
            uint16_t signBit = static_cast<uint16_t>((x[9] >> k) & 0x01);
            op->coeffs[i + k] = static_cast<int16_t>(
                (((-signBit) ^ static_cast<uint16_t>(op->coeffs[i + k])) + signBit)
                << P::Mod16LogQ);
        }
        j += P::DgRandBits;
    }
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::dGaussian(PolyVec* op, const uint8_t seed[P::CryptoBytes]) {
    uint8_t extseed[P::CryptoBytes + 1] = {};
    std::memcpy(extseed, seed, P::CryptoBytes);
    for (int i = 0; i < P::K; ++i) {
        extseed[P::CryptoBytes] = static_cast<uint8_t>(P::K * i);
        dGaussianPoly(&(op->vec[i]), extseed);
    }
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::spCbd(Poly* r, const uint8_t* buf) {
    if constexpr (Mode == SmaugtMode::Mode1 || Mode == SmaugtMode::ModeT) {
        // sp_cbd1: eta=2, p(0)=3/4, p(1)=p(-1)=1/8
        for (unsigned int i = 0; i < P::N / 8; i++) {
            uint32_t t = static_cast<uint32_t>(buf[3 * i]) |
                         (static_cast<uint32_t>(buf[3 * i + 1]) << 8) |
                         (static_cast<uint32_t>(buf[3 * i + 2]) << 16);
            uint32_t d = t & 0x00249249u;
            d &= (t >> 1) & 0x00249249u;
            uint32_t s = (t >> 2) & 0x00249249u;
            for (unsigned int j = 0; j < 8; j++) {
                int16_t a = static_cast<int16_t>((d >> (3 * j)) & 0x1);
                r->coeffs[8 * i + j] = static_cast<int16_t>(
                    a * static_cast<int16_t>((((static_cast<int>(((s >> (3 * j)) & 0x1)) - 1) ^ -2) | 1)));
            }
        }
    } else if constexpr (Mode == SmaugtMode::Mode3) {
        // cbd: eta=1
        for (unsigned int i = 0; i < P::N / 16; i++) {
            uint32_t t = static_cast<uint32_t>(buf[4 * i]) |
                         (static_cast<uint32_t>(buf[4 * i + 1]) << 8) |
                         (static_cast<uint32_t>(buf[4 * i + 2]) << 16) |
                         (static_cast<uint32_t>(buf[4 * i + 3]) << 24);
            for (unsigned int j = 0; j < 16; j++) {
                int16_t a = static_cast<int16_t>((t >> (2 * j + 0)) & 0x01);
                int16_t b = static_cast<int16_t>((t >> (2 * j + 1)) & 0x01);
                r->coeffs[16 * i + j] = a - b;
            }
        }
    } else if constexpr (Mode == SmaugtMode::Mode5) {
        // sp_cbd2: eta=2, p(0)=5/8, p(1)=p(-1)=3/16
        for (unsigned int i = 0; i < P::N / 8; i++) {
            uint32_t t = static_cast<uint32_t>(buf[4 * i]) |
                         (static_cast<uint32_t>(buf[4 * i + 1]) << 8) |
                         (static_cast<uint32_t>(buf[4 * i + 2]) << 16) |
                         (static_cast<uint32_t>(buf[4 * i + 3]) << 24);
            uint32_t d = t & 0x11111111u;
            d |= (t >> 1) & 0x11111111u;
            d &= (t >> 2) & 0x11111111u;
            uint32_t s = (t >> 3) & 0x11111111u;
            for (unsigned int j = 0; j < 8; j++) {
                int16_t a = static_cast<int16_t>((d >> (4 * j)) & 0x1);
                r->coeffs[8 * i + j] = static_cast<int16_t>(
                    a * static_cast<int16_t>((((static_cast<int>(((s >> (4 * j)) & 0x1)) - 1) ^ -2) | 1)));
            }
        }
    }
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::expandR(PolyVec* r, const uint8_t* seed) {
    uint8_t buf[P::CbdSeedBytes] = {};
    for (int i = 0; i < P::K; ++i) {
        uint8_t extseed[P::DeltaBytes + 1];
        std::memcpy(extseed, seed, P::DeltaBytes);
        extseed[P::DeltaBytes] = static_cast<uint8_t>(i);
        Shake256::hash(buf, P::CbdSeedBytes, extseed, P::DeltaBytes + 1);
        spCbd(&r->vec[i], buf);
    }
}

// ============================================================================
// 키 확장
// ============================================================================

template<SmaugtMode Mode>
void SmaugtKem<Mode>::expandA(PolyVec A[P::K], const uint8_t seed[P::PkSeedBytes]) {
    uint8_t buf[P::PkPolyBytes] = {};
    uint8_t extseed[P::PkSeedBytes + 2];
    std::memcpy(extseed, seed, P::PkSeedBytes);
    for (int i = 0; i < P::K; i++) {
        for (int j = 0; j < P::K; j++) {
            extseed[32] = static_cast<uint8_t>(i);
            extseed[33] = static_cast<uint8_t>(j);
            Shake128::hash(buf, P::PkPolyBytes, extseed, P::PkSeedBytes + 2);
            unpackRing(&A[i].vec[j], buf);
        }
    }
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::expandB(PolyVec* b, const PolyVec A[P::K],
                              const PolyVec* s,
                              const uint8_t eSeed[P::CryptoBytes]) {
    dGaussian(b, eSeed);
    matVecMultSub(b, A, s);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::genPubKey(PublicKeyInternal* pk, const PolyVec* sk,
                                const uint8_t errSeed[P::CryptoBytes]) {
    expandA(pk->A, pk->seed);
    std::memset(&(pk->b), 0, sizeof(uint16_t) * P::N);
    expandB(&(pk->b), pk->A, sk, errSeed);
}

// ============================================================================
// 패킹/언패킹 — R_q
// ============================================================================

template<SmaugtMode Mode>
void SmaugtKem<Mode>::packRing(uint8_t* bytes, const Poly* data) {
    std::memset(bytes, 0, P::PkPolyBytes);
    if constexpr (P::LogQ == 10) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 4) {
            uint64_t packed = 0;
            for (size_t i = 0; i < 4; ++i)
                packed |= (static_cast<uint64_t>((data->coeffs[di + i] >> 6) & 0x03ff) << (10 * i));
            bytes[bi++] = static_cast<uint8_t>(packed);
            bytes[bi++] = static_cast<uint8_t>(packed >> 8);
            bytes[bi++] = static_cast<uint8_t>(packed >> 16);
            bytes[bi++] = static_cast<uint8_t>(packed >> 24);
            bytes[bi++] = static_cast<uint8_t>(packed >> 32);
        }
    } else if constexpr (P::LogQ == 11) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 8) {
            uint64_t packed = 0;
            uint32_t packed2 = 0;
            for (size_t i = 0; i < 5; ++i)
                packed |= (static_cast<uint64_t>((data->coeffs[di + i] >> 5) & 0x07ff) << (11 * i));
            packed |= static_cast<uint64_t>((data->coeffs[di + 5] >> 5) & 0x0001) << 55;
            packed2 = static_cast<uint32_t>((data->coeffs[di + 5] >> 6) & 0x03ff);
            for (size_t i = 0; i < 2; ++i)
                packed2 |= (static_cast<uint32_t>((data->coeffs[di + 6 + i] >> 5) & 0x07ff) << (10 + 11 * i));
            for (int i = 0; i < 7; ++i)
                bytes[bi++] = static_cast<uint8_t>(packed >> (8 * i));
            for (int i = 0; i < 4; ++i)
                bytes[bi++] = static_cast<uint8_t>(packed2 >> (8 * i));
        }
    }
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::unpackRing(Poly* data, const uint8_t* bytes) {
    std::memset(data, 0, sizeof(int16_t) * P::N);
    if constexpr (P::LogQ == 10) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 4) {
            uint64_t packed = 0;
            for (int i = 0; i < 5; ++i)
                packed |= static_cast<uint64_t>(bytes[bi++]) << (8 * i);
            for (size_t i = 0; i < 4; i++)
                data->coeffs[di + i] = static_cast<int16_t>(((packed >> (10 * i)) & 0x03ff) << 6);
        }
    } else if constexpr (P::LogQ == 11) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 8) {
            uint64_t packed = 0;
            for (int i = 0; i < 7; ++i)
                packed |= static_cast<uint64_t>(bytes[bi++]) << (8 * i);
            uint32_t packed2 = 0;
            for (int i = 0; i < 4; ++i)
                packed2 |= static_cast<uint32_t>(bytes[bi++]) << (8 * i);
            for (size_t i = 0; i < 5; i++)
                data->coeffs[di + i] = static_cast<int16_t>(((packed >> (11 * i)) & 0x07ff) << 5);
            data->coeffs[di + 5] = static_cast<int16_t>(((packed >> 55) & 0x0001) << 5);
            data->coeffs[di + 5] |= static_cast<int16_t>((packed2 & 0x03ff) << 6);
            for (size_t i = 0; i < 2; i++)
                data->coeffs[di + 6 + i] = static_cast<int16_t>(((packed2 >> (10 + 11 * i)) & 0x07ff) << 5);
        }
    }
}

// ============================================================================
// 패킹/언패킹 — R_p
// ============================================================================

template<SmaugtMode Mode>
void SmaugtKem<Mode>::packRingP(uint8_t* bytes, const Poly* data) {
    std::memset(bytes, 0, P::CtPoly1Bytes);
    if constexpr (P::LogP == 8) {
        for (size_t i = 0; i < P::N; ++i)
            bytes[i] = static_cast<uint8_t>(data->coeffs[i] & 0x00ff);
    } else if constexpr (P::LogP == 9) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 8) {
            uint64_t packed = 0;
            uint8_t packed2 = 0;
            for (size_t i = 0; i < 7; ++i)
                packed |= (static_cast<uint64_t>(data->coeffs[di + i] & 0x01ff) << (9 * i));
            packed |= static_cast<uint64_t>(data->coeffs[di + 7] & 0x0001) << 63;
            packed2 = static_cast<uint8_t>(data->coeffs[di + 7] >> 1);
            for (int i = 0; i < 8; ++i)
                bytes[bi++] = static_cast<uint8_t>(packed >> (8 * i));
            bytes[bi++] = packed2;
        }
    }
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::unpackRingP(Poly* data, const uint8_t* bytes) {
    std::memset(data, 0, sizeof(int16_t) * P::N);
    if constexpr (P::LogP == 8) {
        for (size_t i = 0; i < P::N; ++i)
            data->coeffs[i] = static_cast<int16_t>(bytes[i] & 0xff);
    } else if constexpr (P::LogP == 9) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 8) {
            uint64_t packed = 0;
            for (int i = 0; i < 8; ++i)
                packed |= static_cast<uint64_t>(bytes[bi++]) << (8 * i);
            uint8_t packed2 = bytes[bi++];
            for (size_t i = 0; i < 7; i++)
                data->coeffs[di + i] = static_cast<int16_t>((packed >> (9 * i)) & 0x01ff);
            data->coeffs[di + 7] = static_cast<int16_t>((packed >> 63) & 0x0001);
            data->coeffs[di + 7] |= static_cast<int16_t>((packed2 << 1) & 0x01fe);
        }
    }
}

// ============================================================================
// 패킹/언패킹 — R_p'
// ============================================================================

template<SmaugtMode Mode>
void SmaugtKem<Mode>::packRingPPrime(uint8_t* bytes, const Poly* data) {
    std::memset(bytes, 0, P::CtPoly2Bytes);
    if constexpr (P::LogPPrime == 3) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 8) {
            uint32_t packed = 0;
            for (size_t i = 0; i < 8; ++i)
                packed |= (static_cast<uint32_t>(data->coeffs[di + i] & 0x0007) << (3 * i));
            bytes[bi++] = static_cast<uint8_t>(packed);
            bytes[bi++] = static_cast<uint8_t>(packed >> 8);
            bytes[bi++] = static_cast<uint8_t>(packed >> 16);
        }
    } else if constexpr (P::LogPPrime == 4) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 2) {
            bytes[bi] = static_cast<uint8_t>(data->coeffs[di] & 0x000f);
            bytes[bi++] |= static_cast<uint8_t>((data->coeffs[di + 1] & 0x000f) << 4);
        }
    } else if constexpr (P::LogPPrime == 5) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 8) {
            uint64_t packed = 0;
            for (size_t i = 0; i < 8; ++i)
                packed |= (static_cast<uint64_t>(data->coeffs[di + i] & 0x001f) << (5 * i));
            for (int i = 0; i < 5; ++i)
                bytes[bi++] = static_cast<uint8_t>(packed >> (8 * i));
        }
    } else if constexpr (P::LogPPrime == 7) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 8) {
            uint64_t packed = 0;
            for (size_t i = 0; i < 8; ++i)
                packed |= (static_cast<uint64_t>(data->coeffs[di + i] & 0x007f) << (7 * i));
            for (int i = 0; i < 7; ++i)
                bytes[bi++] = static_cast<uint8_t>(packed >> (8 * i));
        }
    }
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::unpackRingPPrime(Poly* data, const uint8_t* bytes) {
    std::memset(data, 0, sizeof(int16_t) * P::N);
    if constexpr (P::LogPPrime == 3) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 8) {
            uint32_t packed = static_cast<uint32_t>(bytes[bi]) |
                              (static_cast<uint32_t>(bytes[bi + 1]) << 8) |
                              (static_cast<uint32_t>(bytes[bi + 2]) << 16);
            bi += 3;
            for (size_t i = 0; i < 8; i++)
                data->coeffs[di + i] = static_cast<int16_t>((packed >> (3 * i)) & 0x07);
        }
    } else if constexpr (P::LogPPrime == 4) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 2) {
            data->coeffs[di] = static_cast<int16_t>(bytes[bi] & 0x0f);
            data->coeffs[di + 1] = static_cast<int16_t>((bytes[bi++] >> 4) & 0x0f);
        }
    } else if constexpr (P::LogPPrime == 5) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 8) {
            uint64_t packed = 0;
            for (int i = 0; i < 5; ++i)
                packed |= static_cast<uint64_t>(bytes[bi++]) << (8 * i);
            for (size_t i = 0; i < 8; i++)
                data->coeffs[di + i] = static_cast<int16_t>((packed >> (5 * i)) & 0x1f);
        }
    } else if constexpr (P::LogPPrime == 7) {
        size_t bi = 0;
        for (size_t di = 0; di < P::N; di += 8) {
            uint64_t packed = 0;
            for (int i = 0; i < 7; ++i)
                packed |= static_cast<uint64_t>(bytes[bi++]) << (8 * i);
            for (size_t i = 0; i < 8; i++)
                data->coeffs[di + i] = static_cast<int16_t>((packed >> (7 * i)) & 0x7f);
        }
    }
}

// ============================================================================
// 벡터 패킹/언패킹
// ============================================================================

template<SmaugtMode Mode>
void SmaugtKem<Mode>::packRingVec(uint8_t* bytes, const PolyVec* data) {
    for (int i = 0; i < P::K; ++i)
        packRing(bytes + i * P::PkPolyBytes, &(data->vec[i]));
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::unpackRingVec(PolyVec* data, const uint8_t* bytes) {
    for (int i = 0; i < P::K; ++i)
        unpackRing(&(data->vec[i]), bytes + i * P::PkPolyBytes);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::packRingPVec(uint8_t* bytes, const PolyVec* data) {
    for (int i = 0; i < P::K; ++i)
        packRingP(bytes + i * P::CtPoly1Bytes, &(data->vec[i]));
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::unpackRingPVec(PolyVec* data, const uint8_t* bytes) {
    for (int i = 0; i < P::K; ++i)
        unpackRingP(&(data->vec[i]), bytes + i * P::CtPoly1Bytes);
}

// ============================================================================
// 비밀 키 패킹
// ============================================================================

template<SmaugtMode Mode>
void SmaugtKem<Mode>::packSPoly(uint8_t* bytes, const Poly* s) {
    for (unsigned int i = 0; i < P::N / 4; ++i) {
        int di = i * 4;
        bytes[i] = static_cast<uint8_t>(
            ((1 - s->coeffs[di]) & 0x03) |
            (((1 - s->coeffs[di + 1]) & 0x03) << 2) |
            (((1 - s->coeffs[di + 2]) & 0x03) << 4) |
            (((1 - s->coeffs[di + 3]) & 0x03) << 6));
    }
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::unpackSPoly(Poly* s, const uint8_t* bytes) {
    for (unsigned int i = 0; i < P::N / 4; ++i) {
        int di = i * 4;
        uint8_t t0 = bytes[i] & 0x03;
        uint8_t t1 = (bytes[i] >> 2) & 0x03;
        uint8_t t2 = (bytes[i] >> 4) & 0x03;
        uint8_t t3 = (bytes[i] >> 6) & 0x03;
        s->coeffs[di]     = static_cast<int16_t>(1 - t0);
        s->coeffs[di + 1] = static_cast<int16_t>(1 - t1);
        s->coeffs[di + 2] = static_cast<int16_t>(1 - t2);
        s->coeffs[di + 3] = static_cast<int16_t>(1 - t3);
    }
}

// ============================================================================
// 공개 키 / 비밀 키 / 암호문 패킹
// ============================================================================

template<SmaugtMode Mode>
void SmaugtKem<Mode>::packEnck(uint8_t* output, const PublicKeyInternal* pk) {
    std::memcpy(output, pk->seed, P::PkSeedBytes);
    packRingVec(output + P::PkSeedBytes, &(pk->b));
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::unpackEnck(PublicKeyInternal* pk, const uint8_t* input) {
    std::memcpy(pk->seed, input, P::PkSeedBytes);
    expandA(pk->A, pk->seed);
    unpackRingVec(&(pk->b), input + P::PkSeedBytes);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::packDeck(uint8_t* output, const PolyVec* sk) {
    for (int i = 0; i < P::K; ++i)
        packSPoly(output + P::SkPolyBytes * i, &sk->vec[i]);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::unpackDeck(PolyVec* sk, const uint8_t* input) {
    for (int i = 0; i < P::K; ++i)
        unpackSPoly(&sk->vec[i], input + P::SkPolyBytes * i);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::packCt(uint8_t* output, const CiphertextInternal* ct) {
    packRingPVec(output, &(ct->c1));
    packRingPPrime(output + P::CtPolyVecBytes, &(ct->c2));
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::unpackCt(CiphertextInternal* ct, const uint8_t* input) {
    unpackRingPVec(&(ct->c1), input);
    unpackRingPPrime(&(ct->c2), input + P::CtPolyVecBytes);
}

// ============================================================================
// D2 인코딩/디코딩 (ModeT 전용)
// ============================================================================

template<SmaugtMode Mode>
void SmaugtKem<Mode>::d2Encode(Poly* r, const uint8_t* msg) {
    if constexpr (Mode == SmaugtMode::ModeT) {
        for (size_t i = 0; i < P::MsgBytes; i++) {
            for (size_t j = 0; j < 8; j++) {
                unsigned int mask = (msg[i] >> j) & 1;
                mask = (mask * SmaugtParams<SmaugtMode::ModeT>::ModulusScaledQHalf) &
                       SmaugtParams<SmaugtMode::ModeT>::ModulusScaledQHalf;
                r->coeffs[8 * i + j] = static_cast<int16_t>(mask);
                r->coeffs[8 * i + j + 128] = static_cast<int16_t>(mask);
            }
        }
    }
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::d2Decode(uint8_t* msg, const Poly* x) {
    if constexpr (Mode == SmaugtMode::ModeT) {
        constexpr int qHalf = SmaugtParams<SmaugtMode::ModeT>::ModulusScaledQHalf;
        for (size_t i = 0; i < P::MsgBytes; i++) msg[i] = 0;
        for (size_t i = 0; i < P::N / 2; i++) {
            auto flipabs = [](uint16_t val) -> uint16_t {
                int16_t r = static_cast<int16_t>(val) - qHalf;
                int16_t m = r >> 15;
                return static_cast<uint16_t>((r + m) ^ m);
            };
            uint16_t t = flipabs(static_cast<uint16_t>(x->coeffs[i]));
            t += flipabs(static_cast<uint16_t>(x->coeffs[i + 128]));
            t = t - static_cast<uint16_t>(qHalf);
            t >>= 15;
            msg[i >> 3] |= static_cast<uint8_t>(t << (i & 7));
        }
    }
}

// ============================================================================
// 암호문 계산
// ============================================================================

template<SmaugtMode Mode>
void SmaugtKem<Mode>::round1(PolyVec* a) {
    for (int i = 0; i < P::K; ++i)
        for (unsigned int j = 0; j < P::N; ++j)
            a->vec[i].coeffs[j] = static_cast<int16_t>(
                ((a->vec[i].coeffs[j] + P::RdAdd) & P::RdAnd) >> P::Mod16LogP);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::round2(Poly* a) {
    for (unsigned int i = 0; i < P::N; ++i)
        a->coeffs[i] = static_cast<int16_t>(
            ((a->coeffs[i] + P::RdAdd2) & P::RdAnd2) >> P::Mod16LogPPrime);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::computeC1(PolyVec* c1, const PolyVec A[P::K],
                                const PolyVec* r) {
    matVecMultAdd(c1, A, r);
    round1(c1);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::computeC2(Poly* c2, const uint8_t* mu,
                                const PolyVec* b, const PolyVec* r) {
    if constexpr (Mode == SmaugtMode::ModeT) {
        d2Encode(c2, mu);
    } else {
        for (unsigned int i = 0; i < P::MsgBytes; ++i) {
            for (unsigned int j = 0; j < 8; ++j) {
                c2->coeffs[8 * i + j] = static_cast<int16_t>(
                    static_cast<uint16_t>((mu[i] >> j) << P::Mod16LogT));
            }
        }
    }
    vecVecMultAdd(c2, b, r, P::Mod16LogQ);
    round2(c2);
}

// ============================================================================
// IND-CPA PKE
// ============================================================================

template<SmaugtMode Mode>
void SmaugtKem<Mode>::indcpaKeypair(uint8_t* pk, uint8_t* sk,
                                    const uint8_t* seed) {
    PublicKeyInternal pkTmp;
    PolyVec skTmp;
    std::memset(&pkTmp, 0, sizeof(PublicKeyInternal));
    std::memset(&skTmp, 0, sizeof(PolyVec));

    uint8_t extseed[P::CryptoBytes + P::PkSeedBytes] = {};
    static_assert(P::CryptoBytes + P::PkSeedBytes == 64);
    auto digest = Sha3_512::hash(seed, P::CryptoBytes);
    std::memcpy(extseed, digest.data(), 64);

    expandS(&skTmp, extseed);
    std::memcpy(pkTmp.seed, extseed + P::CryptoBytes, P::PkSeedBytes);
    genPubKey(&pkTmp, &skTmp, extseed);

    std::memset(pk, 0, P::PublicKeyBytes);
    std::memset(sk, 0, P::PkeSecretKeyBytes);
    packEnck(pk, &pkTmp);
    packDeck(sk, &skTmp);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::indcpaEnc(uint8_t* ctxt, const uint8_t* pk,
                                const uint8_t* mu, const uint8_t* seed) {
    uint8_t seedR[P::DeltaBytes] = {};
    PublicKeyInternal pkTmp;
    unpackEnck(&pkTmp, pk);

    PolyVec r;
    std::memset(&r, 0, sizeof(PolyVec));

    if (seed == nullptr) {
        SystemRandom rng;
        rng.generate(seedR, P::DeltaBytes);
    } else {
        ctCmov(seedR, seed, P::DeltaBytes, 1);
    }
    expandR(&r, seedR);

    CiphertextInternal ctxtTmp;
    std::memset(&ctxtTmp, 0, sizeof(CiphertextInternal));
    computeC1(&(ctxtTmp.c1), pkTmp.A, &r);
    computeC2(&(ctxtTmp.c2), mu, &pkTmp.b, &r);

    packCt(ctxt, &ctxtTmp);
}

template<SmaugtMode Mode>
void SmaugtKem<Mode>::indcpaDec(uint8_t* mu, const uint8_t* sk,
                                const uint8_t* ctxt) {
    Poly delta;
    PolyVec c1;

    PolyVec skTmp;
    std::memset(&skTmp, 0, sizeof(PolyVec));
    unpackDeck(&skTmp, sk);

    CiphertextInternal ctxtTmp;
    unpackCt(&ctxtTmp, ctxt);

    c1 = ctxtTmp.c1;
    delta = ctxtTmp.c2;
    for (unsigned int i = 0; i < P::N; ++i)
        delta.coeffs[i] <<= P::Mod16LogPPrime;
    for (int i = 0; i < P::K; ++i)
        for (unsigned int j = 0; j < P::N; ++j)
            c1.vec[i].coeffs[j] <<= P::Mod16LogP;

    vecVecMultAdd(&delta, &c1, &skTmp, P::Mod16LogP);

    if constexpr (Mode == SmaugtMode::ModeT) {
        d2Decode(mu, &delta);
    } else {
        for (unsigned int i = 0; i < P::N; ++i) {
            delta.coeffs[i] = static_cast<int16_t>(delta.coeffs[i] + P::DecAdd);
            delta.coeffs[i] >>= P::Mod16LogT;
            delta.coeffs[i] &= 0x01;
        }
        std::memset(mu, 0, P::MsgBytes);
        for (unsigned int i = 0; i < P::MsgBytes; ++i)
            for (unsigned int j = 0; j < 8; ++j)
                mu[i] ^= (static_cast<uint8_t>(delta.coeffs[8 * i + j]) << j);
    }
}

// ============================================================================
// KEM 공개 API
// ============================================================================

template<SmaugtMode Mode>
typename SmaugtKem<Mode>::KeyPair SmaugtKem<Mode>::keygen() {
    KeyPair kp;
    kp.publicKey.resize(P::PublicKeyBytes);
    kp.secretKey.resize(P::KemSecretKeyBytes);

    uint8_t d[P::TBytes] = {};
    uint8_t seed[P::CryptoBytes] = {};
    SystemRandom rng;
    rng.generate(d, P::TBytes);
    rng.generate(seed, P::CryptoBytes);

    indcpaKeypair(kp.publicKey.data(), kp.secretKey.data(), seed);
    std::memcpy(kp.secretKey.data() + P::PkeSecretKeyBytes, d, P::TBytes);
    std::memcpy(kp.secretKey.data() + P::PkeSecretKeyBytes + P::TBytes,
                kp.publicKey.data(), P::PublicKeyBytes);

    return kp;
}

template<SmaugtMode Mode>
typename SmaugtKem<Mode>::EncapsResult SmaugtKem<Mode>::encapsulate(const uint8_t* pk) {
    EncapsResult result;
    result.ciphertext.resize(P::CiphertextBytes);
    result.sharedSecret.resize(P::SharedSecretBytes, 0);

    uint8_t mu[P::MsgBytes] = {};
    SystemRandom rng;
    rng.generate(mu, P::MsgBytes);

    uint8_t seedR[P::DeltaBytes + P::CryptoBytes] = {};
    hashH(seedR, pk, P::PublicKeyBytes);
    hashG(seedR, P::DeltaBytes + P::CryptoBytes,
          mu, P::MsgBytes, seedR, Sha3_256::kDigestSize);

    indcpaEnc(result.ciphertext.data(), pk, mu, seedR);
    ctCmov(result.sharedSecret.data(), seedR + P::DeltaBytes, P::CryptoBytes, 1);

    return result;
}

template<SmaugtMode Mode>
std::vector<uint8_t> SmaugtKem<Mode>::decapsulate(const uint8_t* ciphertext,
                                                   const uint8_t* sk) {
    std::vector<uint8_t> ss(P::SharedSecretBytes, 0);

    uint8_t mu[P::MsgBytes] = {};
    uint8_t buf[P::DeltaBytes + P::CryptoBytes] = {};
    uint8_t bufTmp[P::DeltaBytes + P::CryptoBytes] = {};
    uint8_t hashRes[Sha3_256::kDigestSize] = {};
    const uint8_t* pk = sk + P::PkeSecretKeyBytes + P::TBytes;

    indcpaDec(mu, sk, ciphertext);
    hashH(hashRes, pk, P::PublicKeyBytes);
    hashG(buf, P::DeltaBytes + P::CryptoBytes,
          mu, P::MsgBytes, hashRes, Sha3_256::kDigestSize);

    uint8_t ctxtTemp[P::CiphertextBytes] = {};
    indcpaEnc(ctxtTemp, pk, mu, buf);

    int fail = ctVerify(ciphertext, ctxtTemp, P::CiphertextBytes);

    hashH(hashRes, ciphertext, P::CiphertextBytes);
    hashG(bufTmp, P::DeltaBytes + P::CryptoBytes,
          sk + P::PkeSecretKeyBytes, P::TBytes, hashRes, Sha3_256::kDigestSize);

    ctCmov(buf + P::DeltaBytes, bufTmp + P::DeltaBytes,
           P::CryptoBytes, static_cast<uint8_t>(fail));
    ctCmov(ss.data(), buf + P::DeltaBytes, P::CryptoBytes, 1);

    return ss;
}

// ============================================================================
// 명시적 템플릿 인스턴스화
// ============================================================================

template class SmaugtKem<SmaugtMode::Mode1>;
template class SmaugtKem<SmaugtMode::Mode3>;
template class SmaugtKem<SmaugtMode::Mode5>;
template class SmaugtKem<SmaugtMode::ModeT>;
