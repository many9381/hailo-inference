#include "SmaugtPoly.h"

#include <cstdint>
#include <cstring>

// ============================================================================
// Toom-Cook 4-way + Karatsuba 다항식 곱셈
//
// Z[x]/(x^256 + 1) 위에서 두 다항식의 곱을 계산한다.
// Reference: SMAUG-T reference implementation (toomcook.c)
// ============================================================================

uint16_t SmaugtPoly::overflowMul(uint16_t x, uint16_t y) {
    return static_cast<uint16_t>(static_cast<uint32_t>(x) * static_cast<uint32_t>(y));
}

void SmaugtPoly::karatsubaSimple(const uint16_t* a1, const uint16_t* b1, uint16_t* result_final) {
    uint16_t d01[kKaratsubaN / 2 - 1];
    uint16_t d0123[kKaratsubaN / 2 - 1];
    uint16_t d23[kKaratsubaN / 2 - 1];
    uint16_t result_d01[kKaratsubaN - 1];

    std::memset(result_d01, 0, (kKaratsubaN - 1) * sizeof(uint16_t));
    std::memset(d01, 0, (kKaratsubaN / 2 - 1) * sizeof(uint16_t));
    std::memset(d0123, 0, (kKaratsubaN / 2 - 1) * sizeof(uint16_t));
    std::memset(d23, 0, (kKaratsubaN / 2 - 1) * sizeof(uint16_t));
    std::memset(result_final, 0, (2 * kKaratsubaN - 1) * sizeof(uint16_t));

    for (int i = 0; i < kKaratsubaN / 4; i++) {
        uint16_t acc1 = a1[i];
        uint16_t acc2 = a1[i + kKaratsubaN / 4];
        uint16_t acc3 = a1[i + 2 * kKaratsubaN / 4];
        uint16_t acc4 = a1[i + 3 * kKaratsubaN / 4];

        for (int j = 0; j < kKaratsubaN / 4; j++) {
            uint16_t acc5 = b1[j];
            uint16_t acc6 = b1[j + kKaratsubaN / 4];

            result_final[i + j + 0 * kKaratsubaN / 4] += overflowMul(acc1, acc5);
            result_final[i + j + 2 * kKaratsubaN / 4] += overflowMul(acc2, acc6);

            uint16_t acc7 = acc5 + acc6;
            uint16_t acc8 = acc1 + acc2;
            d01[i + j] += static_cast<uint16_t>(acc7 * static_cast<uint64_t>(acc8));

            acc7 = b1[j + 2 * kKaratsubaN / 4];
            acc8 = b1[j + 3 * kKaratsubaN / 4];
            result_final[i + j + 4 * kKaratsubaN / 4] += overflowMul(acc7, acc3);
            result_final[i + j + 6 * kKaratsubaN / 4] += overflowMul(acc8, acc4);

            uint16_t acc9  = acc3 + acc4;
            uint16_t acc10 = acc7 + acc8;
            d23[i + j] += overflowMul(acc9, acc10);

            acc5 = acc5 + acc7;
            acc7 = acc1 + acc3;
            result_d01[i + j + 0 * kKaratsubaN / 4] += overflowMul(acc5, acc7);

            acc6 = acc6 + acc8;
            acc8 = acc2 + acc4;
            result_d01[i + j + 2 * kKaratsubaN / 4] += overflowMul(acc6, acc8);

            acc5 = acc5 + acc6;
            acc7 = acc7 + acc8;
            d0123[i + j] += overflowMul(acc5, acc7);
        }
    }

    for (int i = 0; i < kKaratsubaN / 2 - 1; i++) {
        d0123[i] -= result_d01[i + 0 * kKaratsubaN / 4] +
                    result_d01[i + 2 * kKaratsubaN / 4];
        d01[i] -= result_final[i + 0 * kKaratsubaN / 4] +
                  result_final[i + 2 * kKaratsubaN / 4];
        d23[i] -= result_final[i + 4 * kKaratsubaN / 4] +
                  result_final[i + 6 * kKaratsubaN / 4];
    }

    for (int i = 0; i < kKaratsubaN / 2 - 1; i++) {
        result_d01[i + 1 * kKaratsubaN / 4] += d0123[i];
        result_final[i + 1 * kKaratsubaN / 4] += d01[i];
        result_final[i + 5 * kKaratsubaN / 4] += d23[i];
    }

    for (int i = 0; i < kKaratsubaN - 1; i++) {
        result_d01[i] -= result_final[i] + result_final[i + kKaratsubaN];
    }

    for (int i = 0; i < kKaratsubaN - 1; i++) {
        result_final[i + 1 * kKaratsubaN / 2] += result_d01[i];
    }
}

void SmaugtPoly::toomCook4Way(const uint16_t* a1, const uint16_t* b1, uint16_t* result) {
    constexpr uint16_t inv3  = 43691;
    constexpr uint16_t inv9  = 36409;
    constexpr uint16_t inv15 = 61167;

    uint16_t aw1[kNSB], aw2[kNSB], aw3[kNSB], aw4[kNSB], aw5[kNSB], aw6[kNSB], aw7[kNSB];
    uint16_t bw1[kNSB], bw2[kNSB], bw3[kNSB], bw4[kNSB], bw5[kNSB], bw6[kNSB], bw7[kNSB];
    uint16_t w1[kNSBRes] = {}, w2[kNSBRes] = {}, w3[kNSBRes] = {},
             w4[kNSBRes] = {}, w5[kNSBRes] = {}, w6[kNSBRes] = {}, w7[kNSBRes] = {};

    const uint16_t* A0 = a1;
    const uint16_t* A1 = &a1[kNSB];
    const uint16_t* A2 = &a1[2 * kNSB];
    const uint16_t* A3 = &a1[3 * kNSB];
    const uint16_t* B0 = b1;
    const uint16_t* B1 = &b1[kNSB];
    const uint16_t* B2 = &b1[2 * kNSB];
    const uint16_t* B3 = &b1[3 * kNSB];

    // EVALUATION
    for (int j = 0; j < kNSB; ++j) {
        uint16_t r0 = A0[j], r1 = A1[j], r2 = A2[j], r3 = A3[j];
        uint16_t r4 = r0 + r2, r5 = r1 + r3;
        aw3[j] = r4 + r5;
        aw4[j] = r4 - r5;
        r4 = ((r0 << 2) + r2) << 1;
        r5 = (r1 << 2) + r3;
        aw5[j] = r4 + r5;
        aw6[j] = r4 - r5;
        aw2[j] = (r3 << 3) + (r2 << 2) + (r1 << 1) + r0;
        aw7[j] = r0;
        aw1[j] = r3;
    }
    for (int j = 0; j < kNSB; ++j) {
        uint16_t r0 = B0[j], r1 = B1[j], r2 = B2[j], r3 = B3[j];
        uint16_t r4 = r0 + r2, r5 = r1 + r3;
        bw3[j] = r4 + r5;
        bw4[j] = r4 - r5;
        r4 = ((r0 << 2) + r2) << 1;
        r5 = (r1 << 2) + r3;
        bw5[j] = r4 + r5;
        bw6[j] = r4 - r5;
        bw2[j] = (r3 << 3) + (r2 << 2) + (r1 << 1) + r0;
        bw7[j] = r0;
        bw1[j] = r3;
    }

    // MULTIPLICATION
    karatsubaSimple(aw1, bw1, w1);
    karatsubaSimple(aw2, bw2, w2);
    karatsubaSimple(aw3, bw3, w3);
    karatsubaSimple(aw4, bw4, w4);
    karatsubaSimple(aw5, bw5, w5);
    karatsubaSimple(aw6, bw6, w6);
    karatsubaSimple(aw7, bw7, w7);

    // INTERPOLATION
    uint16_t* C = result;
    for (int i = 0; i < kNSBRes; ++i) {
        uint16_t r0 = w1[i], r1 = w2[i], r2 = w3[i], r3 = w4[i];
        uint16_t r4 = w5[i], r5 = w6[i], r6 = w7[i];

        r1 = r1 + r4;
        r5 = r5 - r4;
        r3 = static_cast<uint16_t>((r3 - r2) >> 1);
        r4 = r4 - r0;
        r4 = r4 - (r6 << 6);
        r4 = (r4 << 1) + r5;
        r2 = r2 + r3;
        r1 = r1 - (r2 << 6) - r2;
        r2 = r2 - r6;
        r2 = r2 - r0;
        r1 = r1 + 45 * r2;
        r4 = static_cast<uint16_t>((static_cast<uint16_t>((r4 - (r2 << 3)) * static_cast<uint32_t>(inv3))) >> 3);
        r5 = r5 + r1;
        r1 = static_cast<uint16_t>((static_cast<uint16_t>((r1 + (r3 << 4)) * static_cast<uint32_t>(inv9))) >> 1);
        r3 = static_cast<uint16_t>(-(r3 + r1));
        r5 = static_cast<uint16_t>((static_cast<uint16_t>((30 * r1 - r5) * static_cast<uint32_t>(inv15))) >> 2);
        r2 = r2 - r4;
        r1 = r1 - r5;

        C[i]       += r6;
        C[i + 64]  += r5;
        C[i + 128] += r4;
        C[i + 192] += r3;
        C[i + 256] += r2;
        C[i + 320] += r1;
        C[i + 384] += r0;
    }
}

void SmaugtPoly::polyMulAcc(const int16_t a[kSmaugtN], const int16_t b[kSmaugtN],
                            int16_t res[kSmaugtN]) {
    uint16_t c[2 * kSmaugtN] = {};

    toomCook4Way(reinterpret_cast<const uint16_t*>(a),
                 reinterpret_cast<const uint16_t*>(b), c);

    // reduction mod x^256 + 1
    for (int i = kSmaugtN; i < 2 * kSmaugtN; i++) {
        res[i - kSmaugtN] += static_cast<int16_t>(c[i - kSmaugtN] - c[i]);
    }
}

