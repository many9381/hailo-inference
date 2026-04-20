#pragma once

#include <cstdint>

// ----------------------------------------------------------------------------
// SmaugtPoly
//
// SMAUG-T KEM 에서 사용하는 다항식 타입과 Toom-Cook 4-way 곱셈.
// 모든 다항식은 Z[x]/(x^256 + 1) 위에서 정의된다.
// ----------------------------------------------------------------------------

static constexpr int kSmaugtN = 256;

struct Poly {
    int16_t coeffs[kSmaugtN] = {};
};

class SmaugtPoly {
public:
    // res += a * b  (mod x^256 + 1), Toom-Cook 4-way + Karatsuba 곱셈.
    static void polyMulAcc(const int16_t a[kSmaugtN], const int16_t b[kSmaugtN],
                           int16_t res[kSmaugtN]);

private:
    SmaugtPoly() = delete;

    static constexpr int kKaratsubaN = 64;
    static constexpr int kNSB        = kSmaugtN >> 2;   // 64
    static constexpr int kNSBRes     = 2 * kNSB - 1;    // 127

    static uint16_t overflowMul(uint16_t x, uint16_t y);
    static void karatsubaSimple(const uint16_t* a1, const uint16_t* b1,
                                uint16_t* result_final);
    static void toomCook4Way(const uint16_t* a1, const uint16_t* b1,
                             uint16_t* result);
};
