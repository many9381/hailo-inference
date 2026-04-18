#include "X25519.h"

#include <cstring>

// ============================================================================
// X25519 — RFC 7748 Curve25519 Diffie-Hellman
//
// 필드: GF(p) where p = 2^255 - 19
// 표현: 5 limbs × 51 비트  (f[0] + f[1]*2^51 + f[2]*2^102 + f[3]*2^153 + f[4]*2^204)
// 곱셈에서 __uint128_t 를 사용하여 carry 를 처리한다.
// ============================================================================

// ── 필드 원소 변환 ──────────────────────────────────────────────────────────

void X25519::feFromBytes(Fe out, const uint8_t in[32]) {
    uint64_t t[5];
    t[0] = static_cast<uint64_t>(in[ 0])
         | (static_cast<uint64_t>(in[ 1]) << 8)
         | (static_cast<uint64_t>(in[ 2]) << 16)
         | (static_cast<uint64_t>(in[ 3]) << 24)
         | (static_cast<uint64_t>(in[ 4]) << 32)
         | (static_cast<uint64_t>(in[ 5]) << 40)
         | ((static_cast<uint64_t>(in[ 6]) & 0x07) << 48);

    t[1] = (static_cast<uint64_t>(in[ 6]) >> 3)
         | (static_cast<uint64_t>(in[ 7]) << 5)
         | (static_cast<uint64_t>(in[ 8]) << 13)
         | (static_cast<uint64_t>(in[ 9]) << 21)
         | (static_cast<uint64_t>(in[10]) << 29)
         | (static_cast<uint64_t>(in[11]) << 37)
         | ((static_cast<uint64_t>(in[12]) & 0x3f) << 45);

    t[2] = (static_cast<uint64_t>(in[12]) >> 6)
         | (static_cast<uint64_t>(in[13]) << 2)
         | (static_cast<uint64_t>(in[14]) << 10)
         | (static_cast<uint64_t>(in[15]) << 18)
         | (static_cast<uint64_t>(in[16]) << 26)
         | (static_cast<uint64_t>(in[17]) << 34)
         | (static_cast<uint64_t>(in[18]) << 42)
         | ((static_cast<uint64_t>(in[19]) & 0x01) << 50);

    t[3] = (static_cast<uint64_t>(in[19]) >> 1)
         | (static_cast<uint64_t>(in[20]) << 7)
         | (static_cast<uint64_t>(in[21]) << 15)
         | (static_cast<uint64_t>(in[22]) << 23)
         | (static_cast<uint64_t>(in[23]) << 31)
         | (static_cast<uint64_t>(in[24]) << 39)
         | ((static_cast<uint64_t>(in[25]) & 0x0f) << 47);

    t[4] = (static_cast<uint64_t>(in[25]) >> 4)
         | (static_cast<uint64_t>(in[26]) << 4)
         | (static_cast<uint64_t>(in[27]) << 12)
         | (static_cast<uint64_t>(in[28]) << 20)
         | (static_cast<uint64_t>(in[29]) << 28)
         | (static_cast<uint64_t>(in[30]) << 36)
         | ((static_cast<uint64_t>(in[31]) & 0x7f) << 44);

    for (int i = 0; i < 5; ++i)
        out[i] = t[i];
}

void X25519::feReduce(Fe f) {
    // 각 limb 를 51 비트로 줄이고 carry 를 전파
    for (int i = 0; i < 4; ++i) {
        f[i + 1] += f[i] >> 51;
        f[i] &= 0x7ffffffffffff;  // 2^51 - 1
    }
    // 최상위 limb 의 carry: f[4] 가 51 비트를 초과하면 19 배로 f[0] 에 더함
    uint64_t carry = f[4] >> 51;
    f[4] &= 0x7ffffffffffff;
    f[0] += carry * 19;
}

void X25519::feToBytes(uint8_t out[32], const Fe in) {
    Fe t;
    feCopy(t, in);

    // 완전 정규화: [0, p) 범위로 축소
    feReduce(t);
    feReduce(t);

    // t >= p 인 경우 t -= p
    // p = 2^255 - 19 이므로 t + 19 >= 2^255 인지 검사
    uint64_t u = t[0] + 19;
    for (int i = 0; i < 4; ++i) {
        u >>= 51;
        u += t[i + 1];
    }
    u >>= 51;  // u 가 1 이면 t >= p

    uint64_t mask = -(u & 1);  // all-ones if t >= p, else 0
    t[0] += 19 & mask;
    feReduce(t);

    // little-endian 바이트로 변환
    uint8_t* p = out;
    // limb 0: 51 bits → bytes 0-6 (6*8=48, 남은 3비트는 byte 6)
    uint64_t v = t[0] | (t[1] << 51);
    // 실제로는 5 limb 를 하나의 큰 정수로 합쳐서 바이트로 쓰는 것이 정확
    // 아래는 직접 바이트 추출
    std::memset(out, 0, 32);

    // 256 비트 정수를 little-endian 으로 쓴다
    // 총 비트: 51*5 = 255 비트
    uint64_t acc = 0;
    int bits = 0;
    int pos = 0;
    for (int i = 0; i < 5; ++i) {
        acc |= t[i] << bits;
        bits += 51;
        while (bits >= 8 && pos < 32) {
            p[pos++] = static_cast<uint8_t>(acc & 0xff);
            acc >>= 8;
            bits -= 8;
        }
    }
    if (pos < 32) {
        p[pos] = static_cast<uint8_t>(acc & 0xff);
    }
}

// ── 필드 산술 ────────────────────────────────────────────────────────────────

void X25519::feCopy(Fe out, const Fe in) {
    for (int i = 0; i < 5; ++i) out[i] = in[i];
}

void X25519::feAdd(Fe out, const Fe a, const Fe b) {
    for (int i = 0; i < 5; ++i)
        out[i] = a[i] + b[i];
}

void X25519::feSub(Fe out, const Fe a, const Fe b) {
    // underflow 방지: 2p 를 더한 뒤 뺀다
    // 2p 의 각 limb: 2*(2^51-1) 로 충분하지만 정확히는
    // limb별로 2*p 를 적절히 분배한 값을 사용
    static const uint64_t two_p[5] = {
        0xfffffffffffda,   // 2*(2^51 - 19) = 2^52 - 38
        0xffffffffffffe,   // 2*(2^51 - 1)
        0xffffffffffffe,
        0xffffffffffffe,
        0xffffffffffffe
    };
    for (int i = 0; i < 5; ++i)
        out[i] = a[i] + two_p[i] - b[i];
}

void X25519::feMul(Fe out, const Fe a, const Fe b) {
    using u128 = __uint128_t;

    u128 t0 = static_cast<u128>(a[0]) * b[0]
            + static_cast<u128>(a[1]) * (b[4] * 19)
            + static_cast<u128>(a[2]) * (b[3] * 19)
            + static_cast<u128>(a[3]) * (b[2] * 19)
            + static_cast<u128>(a[4]) * (b[1] * 19);

    u128 t1 = static_cast<u128>(a[0]) * b[1]
            + static_cast<u128>(a[1]) * b[0]
            + static_cast<u128>(a[2]) * (b[4] * 19)
            + static_cast<u128>(a[3]) * (b[3] * 19)
            + static_cast<u128>(a[4]) * (b[2] * 19);

    u128 t2 = static_cast<u128>(a[0]) * b[2]
            + static_cast<u128>(a[1]) * b[1]
            + static_cast<u128>(a[2]) * b[0]
            + static_cast<u128>(a[3]) * (b[4] * 19)
            + static_cast<u128>(a[4]) * (b[3] * 19);

    u128 t3 = static_cast<u128>(a[0]) * b[3]
            + static_cast<u128>(a[1]) * b[2]
            + static_cast<u128>(a[2]) * b[1]
            + static_cast<u128>(a[3]) * b[0]
            + static_cast<u128>(a[4]) * (b[4] * 19);

    u128 t4 = static_cast<u128>(a[0]) * b[4]
            + static_cast<u128>(a[1]) * b[3]
            + static_cast<u128>(a[2]) * b[2]
            + static_cast<u128>(a[3]) * b[1]
            + static_cast<u128>(a[4]) * b[0];

    // carry 전파
    uint64_t c;

    c = static_cast<uint64_t>(t0 >> 51); t1 += c; out[0] = static_cast<uint64_t>(t0) & 0x7ffffffffffff;
    c = static_cast<uint64_t>(t1 >> 51); t2 += c; out[1] = static_cast<uint64_t>(t1) & 0x7ffffffffffff;
    c = static_cast<uint64_t>(t2 >> 51); t3 += c; out[2] = static_cast<uint64_t>(t2) & 0x7ffffffffffff;
    c = static_cast<uint64_t>(t3 >> 51); t4 += c; out[3] = static_cast<uint64_t>(t3) & 0x7ffffffffffff;
    c = static_cast<uint64_t>(t4 >> 51); out[0] += c * 19; out[4] = static_cast<uint64_t>(t4) & 0x7ffffffffffff;

    // 한 번 더 carry
    c = out[0] >> 51; out[1] += c; out[0] &= 0x7ffffffffffff;
}

void X25519::feSquare(Fe out, const Fe a) {
    using u128 = __uint128_t;

    u128 d0 = static_cast<u128>(a[0]) * 2;
    u128 d1 = static_cast<u128>(a[1]) * 2;
    u128 d2 = static_cast<u128>(a[2]) * 2 * 19;
    u128 d3 = static_cast<u128>(a[3]) * 19;
    u128 d4 = static_cast<u128>(a[4]) * 2 * 19;

    u128 t0 = static_cast<u128>(a[0]) * a[0]
            + d4 * a[1]
            + d2 * a[3];

    u128 t1 = d0 * a[1]
            + d4 * a[2]
            + static_cast<u128>(d3) * a[3];

    u128 t2 = d0 * a[2]
            + static_cast<u128>(a[1]) * a[1]
            + d4 * a[3];

    u128 t3 = d0 * a[3]
            + d1 * a[2]
            + static_cast<u128>(a[4]) * (a[4] * 19);

    u128 t4 = d0 * a[4]
            + d1 * a[3]
            + static_cast<u128>(a[2]) * a[2];

    uint64_t c;

    c = static_cast<uint64_t>(t0 >> 51); t1 += c; out[0] = static_cast<uint64_t>(t0) & 0x7ffffffffffff;
    c = static_cast<uint64_t>(t1 >> 51); t2 += c; out[1] = static_cast<uint64_t>(t1) & 0x7ffffffffffff;
    c = static_cast<uint64_t>(t2 >> 51); t3 += c; out[2] = static_cast<uint64_t>(t2) & 0x7ffffffffffff;
    c = static_cast<uint64_t>(t3 >> 51); t4 += c; out[3] = static_cast<uint64_t>(t3) & 0x7ffffffffffff;
    c = static_cast<uint64_t>(t4 >> 51); out[0] += c * 19; out[4] = static_cast<uint64_t>(t4) & 0x7ffffffffffff;

    c = out[0] >> 51; out[1] += c; out[0] &= 0x7ffffffffffff;
}

void X25519::feMul121666(Fe out, const Fe a) {
    using u128 = __uint128_t;

    u128 t0 = static_cast<u128>(a[0]) * 121666;
    u128 t1 = static_cast<u128>(a[1]) * 121666;
    u128 t2 = static_cast<u128>(a[2]) * 121666;
    u128 t3 = static_cast<u128>(a[3]) * 121666;
    u128 t4 = static_cast<u128>(a[4]) * 121666;

    uint64_t c;

    c = static_cast<uint64_t>(t0 >> 51); t1 += c; out[0] = static_cast<uint64_t>(t0) & 0x7ffffffffffff;
    c = static_cast<uint64_t>(t1 >> 51); t2 += c; out[1] = static_cast<uint64_t>(t1) & 0x7ffffffffffff;
    c = static_cast<uint64_t>(t2 >> 51); t3 += c; out[2] = static_cast<uint64_t>(t2) & 0x7ffffffffffff;
    c = static_cast<uint64_t>(t3 >> 51); t4 += c; out[3] = static_cast<uint64_t>(t3) & 0x7ffffffffffff;
    c = static_cast<uint64_t>(t4 >> 51); out[0] += c * 19; out[4] = static_cast<uint64_t>(t4) & 0x7ffffffffffff;

    c = out[0] >> 51; out[1] += c; out[0] &= 0x7ffffffffffff;
}

void X25519::feCswap(Fe a, Fe b, uint64_t swap) {
    uint64_t mask = ~(swap - 1);  // 0 or all-ones
    for (int i = 0; i < 5; ++i) {
        uint64_t x = (a[i] ^ b[i]) & mask;
        a[i] ^= x;
        b[i] ^= x;
    }
}

void X25519::feInvert(Fe out, const Fe z) {
    // z^(p-2) = z^(2^255 - 21) by Fermat's little theorem
    Fe t0, t1, t2, t3;

    feSquare(t0, z);        // t0 = z^2
    feSquare(t1, t0);       // t1 = z^4
    feSquare(t1, t1);       // t1 = z^8
    feMul(t1, z, t1);       // t1 = z^9
    feMul(t0, t0, t1);      // t0 = z^11
    feSquare(t2, t0);       // t2 = z^22
    feMul(t1, t1, t2);      // t1 = z^(2^5 - 1) = z^31

    feSquare(t2, t1);
    for (int i = 0; i < 4; ++i) feSquare(t2, t2);
    feMul(t1, t2, t1);      // t1 = z^(2^10 - 1)

    feSquare(t2, t1);
    for (int i = 0; i < 9; ++i) feSquare(t2, t2);
    feMul(t2, t2, t1);      // t2 = z^(2^20 - 1)

    feSquare(t3, t2);
    for (int i = 0; i < 19; ++i) feSquare(t3, t3);
    feMul(t2, t3, t2);      // t2 = z^(2^40 - 1)

    feSquare(t2, t2);
    for (int i = 0; i < 9; ++i) feSquare(t2, t2);
    feMul(t1, t2, t1);      // t1 = z^(2^50 - 1)

    feSquare(t2, t1);
    for (int i = 0; i < 49; ++i) feSquare(t2, t2);
    feMul(t2, t2, t1);      // t2 = z^(2^100 - 1)

    feSquare(t3, t2);
    for (int i = 0; i < 99; ++i) feSquare(t3, t3);
    feMul(t2, t3, t2);      // t2 = z^(2^200 - 1)

    feSquare(t2, t2);
    for (int i = 0; i < 49; ++i) feSquare(t2, t2);
    feMul(t1, t2, t1);      // t1 = z^(2^250 - 1)

    feSquare(t1, t1);       // z^(2^251 - 2)
    feSquare(t1, t1);       // z^(2^252 - 4)
    feSquare(t1, t1);       // z^(2^253 - 8)
    feSquare(t1, t1);       // z^(2^254 - 16)
    feSquare(t1, t1);       // z^(2^255 - 32)
    feMul(out, t1, t0);     // z^(2^255 - 32 + 11) = z^(2^255 - 21) = z^(p-2)
}

// ============================================================================
// X25519 스칼라 곱셈 (Montgomery ladder)
// ============================================================================

X25519::Key X25519::scalarMult(const Key& scalar, const Key& point) {
    // 스칼라 클램핑 (RFC 7748 Section 5)
    uint8_t e[32];
    std::memcpy(e, scalar.data(), 32);
    e[0]  &= 248;
    e[31] &= 127;
    e[31] |= 64;

    Fe x1, x2, z2, x3, z3, tmp0;

    feFromBytes(x1, point.data());

    // x2 = 1, z2 = 0, x3 = x1, z3 = 1
    std::memset(x2, 0, sizeof(Fe)); x2[0] = 1;
    std::memset(z2, 0, sizeof(Fe));
    feCopy(x3, x1);
    std::memset(z3, 0, sizeof(Fe)); z3[0] = 1;

    uint64_t swap = 0;

    for (int pos = 254; pos >= 0; --pos) {
        uint64_t b = (e[pos / 8] >> (pos & 7)) & 1;
        feCswap(x2, x3, swap ^ b);
        feCswap(z2, z3, swap ^ b);
        swap = b;

        Fe A, B, C, D, AA, BB, E, DA, CB;

        feAdd(A, x2, z2);       // A = x2 + z2
        feSquare(AA, A);        // AA = A^2
        feSub(B, x2, z2);       // B = x2 - z2
        feSquare(BB, B);        // BB = B^2
        feSub(E, AA, BB);       // E = AA - BB
        feAdd(C, x3, z3);       // C = x3 + z3
        feSub(D, x3, z3);       // D = x3 - z3
        feMul(DA, D, A);        // DA = D * A
        feMul(CB, C, B);        // CB = C * B

        feAdd(tmp0, DA, CB);
        feSquare(x3, tmp0);     // x3 = (DA + CB)^2

        feSub(tmp0, DA, CB);
        feSquare(tmp0, tmp0);
        feMul(z3, x1, tmp0);   // z3 = x1 * (DA - CB)^2

        feMul(x2, AA, BB);     // x2 = AA * BB
        feMul121666(tmp0, E);
        feAdd(tmp0, tmp0, AA);
        feMul(z2, E, tmp0);    // z2 = E * (AA + a24 * E)  where a24 = 121666
    }

    feCswap(x2, x3, swap);
    feCswap(z2, z3, swap);

    // result = x2 * z2^(-1)
    feInvert(z2, z2);
    feMul(x2, x2, z2);

    Key result;
    feToBytes(result.data(), x2);
    return result;
}

X25519::Key X25519::scalarMultBase(const Key& scalar) {
    // 기저점 u = 9
    Key basepoint{};
    basepoint[0] = 9;
    return scalarMult(scalar, basepoint);
}

// ============================================================================
// 공개 API
// ============================================================================

X25519::KeyPair X25519::generateKeyPair(const Key& seed) {
    KeyPair kp;
    kp.privateKey = seed;
    // 클램핑은 scalarMult 내부에서 수행되므로 원본을 보존
    kp.publicKey = scalarMultBase(seed);
    return kp;
}

bool X25519::computeShared(const Key& myPrivate, const Key& peerPublic,
                           Key& sharedOut) {
    sharedOut = scalarMult(myPrivate, peerPublic);

    // all-zero 검사 (low-order point 방지)
    uint8_t acc = 0;
    for (int i = 0; i < 32; ++i)
        acc |= sharedOut[i];

    return acc != 0;
}
