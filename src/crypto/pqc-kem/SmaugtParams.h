#pragma once

#include <cstddef>
#include <cstdint>

// ----------------------------------------------------------------------------
// SMAUG-T KEM 파라미터 정의
//
// 4 가지 보안 모드(Mode1, Mode3, Mode5, ModeT)에 대한 컴파일 타임 상수를 정의.
// SmaugtKem 템플릿 클래스에서 SmaugtParams<Mode> 특수화를 통해 사용된다.
// ----------------------------------------------------------------------------

enum class SmaugtMode { Mode1 = 0, Mode3 = 1, Mode5 = 2, ModeT = 3 };

// 모드 공통 상수 (각 모드 특수화가 상속)
struct SmaugtCommon {
    static constexpr size_t N           = 256;
    static constexpr size_t DeltaBytes  = N / 8;       // 32
    static constexpr size_t TBytes      = N / 8;       // 32
    static constexpr int    LogT        = 1;
    static constexpr size_t SharedSecretBytes = 32;
    static constexpr size_t CryptoBytes = 32;
    static constexpr size_t PkSeedBytes = 32;
    static constexpr size_t HwtSeedBytes = (16 * 308) / 8;  // 616
    static constexpr int    DgRandBits  = 10;
    static constexpr int    DgSLen      = 2;
    static constexpr size_t DgSeedLen   = DgRandBits * N / 64;  // 40
    static constexpr int    DecAdd      = 0x4000;
};

template<SmaugtMode Mode>
struct SmaugtParams;

// ── Mode1: 보안 레벨 1 ─────────────────────────────────────────────────
template<>
struct SmaugtParams<SmaugtMode::Mode1> : SmaugtCommon {
    static constexpr int    K           = 2;
    static constexpr int    LogQ        = 10;
    static constexpr int    LogP        = 8;
    static constexpr int    LogPPrime   = 5;
    static constexpr size_t CbdSeedBytes = (3 * N) / 8;  // 96
    static constexpr size_t MsgBytes    = DeltaBytes;     // 32
    static constexpr int    HS          = 70;

    static constexpr int    RdAdd       = 0x80;
    static constexpr int    RdAnd       = 0xff00;
    static constexpr int    RdAdd2      = 0x0400;
    static constexpr int    RdAnd2      = 0xf800;

    static constexpr int    Mod16LogQ      = 16 - LogQ;       // 6
    static constexpr int    Mod16LogP      = 16 - LogP;       // 8
    static constexpr int    Mod16LogPPrime = 16 - LogPPrime;  // 11
    static constexpr int    Mod16LogT      = 16 - LogT;       // 15

    static constexpr size_t SkPolyBytes    = N / 4;                    // 64
    static constexpr size_t PkPolyBytes    = (LogQ * N) / 8;          // 320
    static constexpr size_t CtPoly1Bytes   = (LogP * N) / 8;          // 256
    static constexpr size_t CtPoly2Bytes   = (LogPPrime * N) / 8;     // 160

    static constexpr size_t SkPolyVecBytes = SkPolyBytes * K;
    static constexpr size_t PkPolyVecBytes = PkPolyBytes * K;
    static constexpr size_t CtPolyVecBytes = CtPoly1Bytes * K;

    static constexpr size_t PublicKeyBytes  = PkSeedBytes + PkPolyVecBytes;
    static constexpr size_t PkeSecretKeyBytes = SkPolyVecBytes;
    static constexpr size_t CiphertextBytes = CtPolyVecBytes + CtPoly2Bytes;
    static constexpr size_t KemSecretKeyBytes = PkeSecretKeyBytes + TBytes + PublicKeyBytes;
};

// ── Mode3: 보안 레벨 3 ─────────────────────────────────────────────────
template<>
struct SmaugtParams<SmaugtMode::Mode3> : SmaugtCommon {
    static constexpr int    K           = 3;
    static constexpr int    LogQ        = 11;
    static constexpr int    LogP        = 9;
    static constexpr int    LogPPrime   = 4;
    static constexpr size_t CbdSeedBytes = (2 * N) / 8;  // 64
    static constexpr size_t MsgBytes    = DeltaBytes;     // 32
    static constexpr int    HS          = 88;

    static constexpr int    RdAdd       = 0x40;
    static constexpr int    RdAnd       = 0xff80;
    static constexpr int    RdAdd2      = 0x0800;
    static constexpr int    RdAnd2      = 0xf000;

    static constexpr int    Mod16LogQ      = 16 - LogQ;
    static constexpr int    Mod16LogP      = 16 - LogP;
    static constexpr int    Mod16LogPPrime = 16 - LogPPrime;
    static constexpr int    Mod16LogT      = 16 - LogT;

    static constexpr size_t SkPolyBytes    = N / 4;
    static constexpr size_t PkPolyBytes    = (LogQ * N) / 8;
    static constexpr size_t CtPoly1Bytes   = (LogP * N) / 8;
    static constexpr size_t CtPoly2Bytes   = (LogPPrime * N) / 8;

    static constexpr size_t SkPolyVecBytes = SkPolyBytes * K;
    static constexpr size_t PkPolyVecBytes = PkPolyBytes * K;
    static constexpr size_t CtPolyVecBytes = CtPoly1Bytes * K;

    static constexpr size_t PublicKeyBytes  = PkSeedBytes + PkPolyVecBytes;
    static constexpr size_t PkeSecretKeyBytes = SkPolyVecBytes;
    static constexpr size_t CiphertextBytes = CtPolyVecBytes + CtPoly2Bytes;
    static constexpr size_t KemSecretKeyBytes = PkeSecretKeyBytes + TBytes + PublicKeyBytes;
};

// ── Mode5: 보안 레벨 5 ─────────────────────────────────────────────────
template<>
struct SmaugtParams<SmaugtMode::Mode5> : SmaugtCommon {
    static constexpr int    K           = 4;
    static constexpr int    LogQ        = 11;
    static constexpr int    LogP        = 9;
    static constexpr int    LogPPrime   = 7;
    static constexpr size_t CbdSeedBytes = (4 * N) / 8;  // 128
    static constexpr size_t MsgBytes    = DeltaBytes;     // 32
    static constexpr int    HS          = 87;

    static constexpr int    RdAdd       = 0x40;
    static constexpr int    RdAnd       = 0xff80;
    static constexpr int    RdAdd2      = 0x0100;
    static constexpr int    RdAnd2      = 0xfe00;

    static constexpr int    Mod16LogQ      = 16 - LogQ;
    static constexpr int    Mod16LogP      = 16 - LogP;
    static constexpr int    Mod16LogPPrime = 16 - LogPPrime;
    static constexpr int    Mod16LogT      = 16 - LogT;

    static constexpr size_t SkPolyBytes    = N / 4;
    static constexpr size_t PkPolyBytes    = (LogQ * N) / 8;
    static constexpr size_t CtPoly1Bytes   = (LogP * N) / 8;
    static constexpr size_t CtPoly2Bytes   = (LogPPrime * N) / 8;

    static constexpr size_t SkPolyVecBytes = SkPolyBytes * K;
    static constexpr size_t PkPolyVecBytes = PkPolyBytes * K;
    static constexpr size_t CtPolyVecBytes = CtPoly1Bytes * K;

    static constexpr size_t PublicKeyBytes  = PkSeedBytes + PkPolyVecBytes;
    static constexpr size_t PkeSecretKeyBytes = SkPolyVecBytes;
    static constexpr size_t CiphertextBytes = CtPolyVecBytes + CtPoly2Bytes;
    static constexpr size_t KemSecretKeyBytes = PkeSecretKeyBytes + TBytes + PublicKeyBytes;
};

// ── ModeT: 특수 모드 ───────────────────────────────────────────────────
template<>
struct SmaugtParams<SmaugtMode::ModeT> : SmaugtCommon {
    static constexpr int    K           = 2;
    static constexpr int    LogQ        = 10;
    static constexpr int    LogP        = 8;
    static constexpr int    LogPPrime   = 3;
    static constexpr size_t CbdSeedBytes = (3 * N) / 8;  // 96
    static constexpr size_t MsgBytes    = 16;             // D2 인코딩
    static constexpr int    HS          = 70;

    static constexpr int    RdAdd       = 0x80;
    static constexpr int    RdAnd       = 0xff00;
    static constexpr int    RdAdd2      = 0x1000;
    static constexpr int    RdAnd2      = 0xe000;

    static constexpr int    Mod16LogQ      = 16 - LogQ;
    static constexpr int    Mod16LogP      = 16 - LogP;
    static constexpr int    Mod16LogPPrime = 16 - LogPPrime;
    static constexpr int    Mod16LogT      = 16 - LogT;

    static constexpr int    ModulusScaledQHalf = 32767;  // D2 용: 2^15 - 1

    static constexpr size_t SkPolyBytes    = N / 4;
    static constexpr size_t PkPolyBytes    = (LogQ * N) / 8;
    static constexpr size_t CtPoly1Bytes   = (LogP * N) / 8;
    static constexpr size_t CtPoly2Bytes   = (LogPPrime * N) / 8;

    static constexpr size_t SkPolyVecBytes = SkPolyBytes * K;
    static constexpr size_t PkPolyVecBytes = PkPolyBytes * K;
    static constexpr size_t CtPolyVecBytes = CtPoly1Bytes * K;

    static constexpr size_t PublicKeyBytes  = PkSeedBytes + PkPolyVecBytes;
    static constexpr size_t PkeSecretKeyBytes = SkPolyVecBytes;
    static constexpr size_t CiphertextBytes = CtPolyVecBytes + CtPoly2Bytes;
    static constexpr size_t KemSecretKeyBytes = PkeSecretKeyBytes + TBytes + PublicKeyBytes;
};
