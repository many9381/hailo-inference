#pragma once

#include "SmaugtParams.h"
#include "SmaugtPoly.h"

#include <array>
#include <cstdint>
#include <vector>

// ----------------------------------------------------------------------------
// SmaugtKem
//
// SMAUG-T (Module-LWR 기반) PQC-KEM 구현.
// IND-CCA 보안을 제공하는 키 캡슐화 메커니즘이다.
//
// 템플릿 파라미터 Mode 로 보안 레벨을 선택한다:
//   SmaugtMode::Mode1 — 보안 레벨 1 (기본값)
//   SmaugtMode::Mode3 — 보안 레벨 3
//   SmaugtMode::Mode5 — 보안 레벨 5
//   SmaugtMode::ModeT — 특수 모드
//
// 사용법:
//   SmaugtKem<> kem;
//   auto [pk, sk] = kem.keygen();
//   auto [ct, ss] = kem.encapsulate(pk.data());
//   auto ss2 = kem.decapsulate(ct.data(), sk.data());
//   // ss == ss2
// ----------------------------------------------------------------------------
template<SmaugtMode Mode = SmaugtMode::Mode1>
class SmaugtKem {
public:
    using P = SmaugtParams<Mode>;

    // ── 크기 상수 ────────────────────────────────────────────────────────
    static constexpr size_t PublicKeyBytes    = P::PublicKeyBytes;
    static constexpr size_t SecretKeyBytes    = P::KemSecretKeyBytes;
    static constexpr size_t CiphertextBytes   = P::CiphertextBytes;
    static constexpr size_t SharedSecretBytes = P::SharedSecretBytes;

    // ── 출력 타입 ────────────────────────────────────────────────────────
    struct KeyPair {
        std::vector<uint8_t> publicKey;
        std::vector<uint8_t> secretKey;
    };

    struct EncapsResult {
        std::vector<uint8_t> ciphertext;
        std::vector<uint8_t> sharedSecret;
    };

    SmaugtKem() = default;

    // 키 쌍 생성 (확률적)
    KeyPair keygen();

    // 캡슐화: 공개 키로 공유 비밀 및 암호문 생성 (확률적)
    EncapsResult encapsulate(const uint8_t* pk);

    // 역캡슐화: 비밀 키와 암호문으로 공유 비밀 복원
    std::vector<uint8_t> decapsulate(const uint8_t* ciphertext,
                                     const uint8_t* sk);

private:
    // ── 내부 다항식 벡터 타입 ────────────────────────────────────────────
    struct PolyVec {
        Poly vec[P::K] = {};
    };

    struct PublicKeyInternal {
        uint8_t seed[P::PkSeedBytes] = {};
        PolyVec A[P::K] = {};
        PolyVec b = {};
    };

    struct CiphertextInternal {
        PolyVec c1 = {};
        Poly    c2 = {};
    };

    // ── 해시 함수 래퍼 ──────────────────────────────────────────────────
    static void hashH(uint8_t out[32], const uint8_t* in, size_t inlen);
    static void hashG(uint8_t* out, size_t outlen,
                      const uint8_t* in1, size_t inlen1,
                      const uint8_t* in2, size_t inlen2);

    // ── 상수 시간 유틸리티 ──────────────────────────────────────────────
    static int  ctVerify(const uint8_t* a, const uint8_t* b, size_t len);
    static void ctCmov(uint8_t* r, const uint8_t* x, size_t len, uint8_t b);

    // ── 샘플링 ──────────────────────────────────────────────────────────
    static int  hwt(int16_t* res, const uint8_t* seed);
    static void expandS(PolyVec* sk, const uint8_t seed[P::CryptoBytes]);
    static void dGaussianPoly(Poly* op, const uint8_t* seed);
    static void dGaussian(PolyVec* op, const uint8_t seed[P::CryptoBytes]);
    static void spCbd(Poly* r, const uint8_t* buf);
    static void expandR(PolyVec* r, const uint8_t* seed);

    // ── 키 확장 ─────────────────────────────────────────────────────────
    static void expandA(PolyVec A[P::K], const uint8_t seed[P::PkSeedBytes]);
    static void expandB(PolyVec* b, const PolyVec A[P::K],
                        const PolyVec* s, const uint8_t eSeed[P::CryptoBytes]);
    static void genPubKey(PublicKeyInternal* pk, const PolyVec* sk,
                          const uint8_t errSeed[P::CryptoBytes]);

    // ── 다항식 산술 ─────────────────────────────────────────────────────
    static void polyAdd(Poly* r, const Poly* a, const Poly* b);
    static void polySub(Poly* r, const Poly* a, const Poly* b);
    static void vecVecMult(Poly* r, const PolyVec* a, const PolyVec* b);
    static void vecVecMultAdd(Poly* r, const PolyVec* a, const PolyVec* b, uint8_t mod);
    static void matVecMultAdd(PolyVec* r, const PolyVec A[P::K], const PolyVec* b);
    static void matVecMultSub(PolyVec* r, const PolyVec A[P::K], const PolyVec* b);

    // ── 암호문 계산 ─────────────────────────────────────────────────────
    static void round1(PolyVec* a);
    static void round2(Poly* a);
    static void computeC1(PolyVec* c1, const PolyVec A[P::K], const PolyVec* r);
    static void computeC2(Poly* c2, const uint8_t* mu,
                          const PolyVec* b, const PolyVec* r);

    // ── 직렬화 (패킹/언패킹) ────────────────────────────────────────────
    // R_q 패킹
    static void packRing(uint8_t* bytes, const Poly* data);
    static void unpackRing(Poly* data, const uint8_t* bytes);
    // R_p 패킹
    static void packRingP(uint8_t* bytes, const Poly* data);
    static void unpackRingP(Poly* data, const uint8_t* bytes);
    // R_p' 패킹
    static void packRingPPrime(uint8_t* bytes, const Poly* data);
    static void unpackRingPPrime(Poly* data, const uint8_t* bytes);
    // 벡터/행렬 패킹
    static void packRingVec(uint8_t* bytes, const PolyVec* data);
    static void unpackRingVec(PolyVec* data, const uint8_t* bytes);
    static void packRingPVec(uint8_t* bytes, const PolyVec* data);
    static void unpackRingPVec(PolyVec* data, const uint8_t* bytes);
    // 비밀 키 패킹
    static void packSPoly(uint8_t* bytes, const Poly* s);
    static void unpackSPoly(Poly* s, const uint8_t* bytes);
    // 공개 키 / 비밀 키 / 암호문 패킹
    static void packEnck(uint8_t* output, const PublicKeyInternal* pk);
    static void unpackEnck(PublicKeyInternal* pk, const uint8_t* input);
    static void packDeck(uint8_t* output, const PolyVec* sk);
    static void unpackDeck(PolyVec* sk, const uint8_t* input);
    static void packCt(uint8_t* output, const CiphertextInternal* ct);
    static void unpackCt(CiphertextInternal* ct, const uint8_t* input);

    // ── IND-CPA PKE ─────────────────────────────────────────────────────
    static void indcpaKeypair(uint8_t* pk, uint8_t* sk, const uint8_t* seed);
    static void indcpaEnc(uint8_t* ctxt, const uint8_t* pk,
                          const uint8_t* mu, const uint8_t* seed);
    static void indcpaDec(uint8_t* mu, const uint8_t* sk, const uint8_t* ctxt);

    // ── D2 인코딩/디코딩 (ModeT 전용) ───────────────────────────────────
    static void d2Encode(Poly* r, const uint8_t* msg);
    static void d2Decode(uint8_t* msg, const Poly* x);
};

// 명시적 인스턴스화 선언 (정의는 SmaugtKem.cpp)
extern template class SmaugtKem<SmaugtMode::Mode1>;
extern template class SmaugtKem<SmaugtMode::Mode3>;
extern template class SmaugtKem<SmaugtMode::Mode5>;
extern template class SmaugtKem<SmaugtMode::ModeT>;
