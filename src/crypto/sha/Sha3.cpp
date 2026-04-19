#include "Sha3.h"

#include <cstring>

// ============================================================================
// Keccak-f[1600] 구현 (FIPS 202)
// SHA3 해시 및 SHAKE XOF 공통 sponge 기반.
// ============================================================================

namespace {

constexpr int kNRounds = 24;

inline uint64_t rol(uint64_t a, int offset) {
    return (a << offset) ^ (a >> (64 - offset));
}

uint64_t load64(const uint8_t x[8]) {
    uint64_t r = 0;
    for (unsigned int i = 0; i < 8; i++)
        r |= static_cast<uint64_t>(x[i]) << (8 * i);
    return r;
}

void store64(uint8_t x[8], uint64_t u) {
    for (unsigned int i = 0; i < 8; i++)
        x[i] = static_cast<uint8_t>(u >> (8 * i));
}

const uint64_t kRoundConstants[kNRounds] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL,
};

void keccakF1600(uint64_t state[25]) {
    uint64_t Aba, Abe, Abi, Abo, Abu;
    uint64_t Aga, Age, Agi, Ago, Agu;
    uint64_t Aka, Ake, Aki, Ako, Aku;
    uint64_t Ama, Ame, Ami, Amo, Amu;
    uint64_t Asa, Ase, Asi, Aso, Asu;
    uint64_t BCa, BCe, BCi, BCo, BCu;
    uint64_t Da, De, Di, Do, Du;
    uint64_t Eba, Ebe, Ebi, Ebo, Ebu;
    uint64_t Ega, Ege, Egi, Ego, Egu;
    uint64_t Eka, Eke, Eki, Eko, Eku;
    uint64_t Ema, Eme, Emi, Emo, Emu;
    uint64_t Esa, Ese, Esi, Eso, Esu;

    Aba = state[0];  Abe = state[1];  Abi = state[2];  Abo = state[3];  Abu = state[4];
    Aga = state[5];  Age = state[6];  Agi = state[7];  Ago = state[8];  Agu = state[9];
    Aka = state[10]; Ake = state[11]; Aki = state[12]; Ako = state[13]; Aku = state[14];
    Ama = state[15]; Ame = state[16]; Ami = state[17]; Amo = state[18]; Amu = state[19];
    Asa = state[20]; Ase = state[21]; Asi = state[22]; Aso = state[23]; Asu = state[24];

    for (int round = 0; round < kNRounds; round += 2) {
        BCa = Aba ^ Aga ^ Aka ^ Ama ^ Asa;
        BCe = Abe ^ Age ^ Ake ^ Ame ^ Ase;
        BCi = Abi ^ Agi ^ Aki ^ Ami ^ Asi;
        BCo = Abo ^ Ago ^ Ako ^ Amo ^ Aso;
        BCu = Abu ^ Agu ^ Aku ^ Amu ^ Asu;

        Da = BCu ^ rol(BCe, 1); De = BCa ^ rol(BCi, 1);
        Di = BCe ^ rol(BCo, 1); Do = BCi ^ rol(BCu, 1);
        Du = BCo ^ rol(BCa, 1);

        Aba ^= Da; BCa = Aba;
        Age ^= De; BCe = rol(Age, 44);
        Aki ^= Di; BCi = rol(Aki, 43);
        Amo ^= Do; BCo = rol(Amo, 21);
        Asu ^= Du; BCu = rol(Asu, 14);
        Eba = BCa ^ ((~BCe) & BCi); Eba ^= kRoundConstants[round];
        Ebe = BCe ^ ((~BCi) & BCo);
        Ebi = BCi ^ ((~BCo) & BCu);
        Ebo = BCo ^ ((~BCu) & BCa);
        Ebu = BCu ^ ((~BCa) & BCe);

        Abo ^= Do; BCa = rol(Abo, 28);
        Agu ^= Du; BCe = rol(Agu, 20);
        Aka ^= Da; BCi = rol(Aka, 3);
        Ame ^= De; BCo = rol(Ame, 45);
        Asi ^= Di; BCu = rol(Asi, 61);
        Ega = BCa ^ ((~BCe) & BCi);
        Ege = BCe ^ ((~BCi) & BCo);
        Egi = BCi ^ ((~BCo) & BCu);
        Ego = BCo ^ ((~BCu) & BCa);
        Egu = BCu ^ ((~BCa) & BCe);

        Abe ^= De; BCa = rol(Abe, 1);
        Agi ^= Di; BCe = rol(Agi, 6);
        Ako ^= Do; BCi = rol(Ako, 25);
        Amu ^= Du; BCo = rol(Amu, 8);
        Asa ^= Da; BCu = rol(Asa, 18);
        Eka = BCa ^ ((~BCe) & BCi);
        Eke = BCe ^ ((~BCi) & BCo);
        Eki = BCi ^ ((~BCo) & BCu);
        Eko = BCo ^ ((~BCu) & BCa);
        Eku = BCu ^ ((~BCa) & BCe);

        Abu ^= Du; BCa = rol(Abu, 27);
        Aga ^= Da; BCe = rol(Aga, 36);
        Ake ^= De; BCi = rol(Ake, 10);
        Ami ^= Di; BCo = rol(Ami, 15);
        Aso ^= Do; BCu = rol(Aso, 56);
        Ema = BCa ^ ((~BCe) & BCi);
        Eme = BCe ^ ((~BCi) & BCo);
        Emi = BCi ^ ((~BCo) & BCu);
        Emo = BCo ^ ((~BCu) & BCa);
        Emu = BCu ^ ((~BCa) & BCe);

        Abi ^= Di; BCa = rol(Abi, 62);
        Ago ^= Do; BCe = rol(Ago, 55);
        Aku ^= Du; BCi = rol(Aku, 39);
        Ama ^= Da; BCo = rol(Ama, 41);
        Ase ^= De; BCu = rol(Ase, 2);
        Esa = BCa ^ ((~BCe) & BCi);
        Ese = BCe ^ ((~BCi) & BCo);
        Esi = BCi ^ ((~BCo) & BCu);
        Eso = BCo ^ ((~BCu) & BCa);
        Esu = BCu ^ ((~BCa) & BCe);

        BCa = Eba ^ Ega ^ Eka ^ Ema ^ Esa;
        BCe = Ebe ^ Ege ^ Eke ^ Eme ^ Ese;
        BCi = Ebi ^ Egi ^ Eki ^ Emi ^ Esi;
        BCo = Ebo ^ Ego ^ Eko ^ Emo ^ Eso;
        BCu = Ebu ^ Egu ^ Eku ^ Emu ^ Esu;

        Da = BCu ^ rol(BCe, 1); De = BCa ^ rol(BCi, 1);
        Di = BCe ^ rol(BCo, 1); Do = BCi ^ rol(BCu, 1);
        Du = BCo ^ rol(BCa, 1);

        Eba ^= Da; BCa = Eba;
        Ege ^= De; BCe = rol(Ege, 44);
        Eki ^= Di; BCi = rol(Eki, 43);
        Emo ^= Do; BCo = rol(Emo, 21);
        Esu ^= Du; BCu = rol(Esu, 14);
        Aba = BCa ^ ((~BCe) & BCi); Aba ^= kRoundConstants[round + 1];
        Abe = BCe ^ ((~BCi) & BCo);
        Abi = BCi ^ ((~BCo) & BCu);
        Abo = BCo ^ ((~BCu) & BCa);
        Abu = BCu ^ ((~BCa) & BCe);

        Ebo ^= Do; BCa = rol(Ebo, 28);
        Egu ^= Du; BCe = rol(Egu, 20);
        Eka ^= Da; BCi = rol(Eka, 3);
        Eme ^= De; BCo = rol(Eme, 45);
        Esi ^= Di; BCu = rol(Esi, 61);
        Aga = BCa ^ ((~BCe) & BCi);
        Age = BCe ^ ((~BCi) & BCo);
        Agi = BCi ^ ((~BCo) & BCu);
        Ago = BCo ^ ((~BCu) & BCa);
        Agu = BCu ^ ((~BCa) & BCe);

        Ebe ^= De; BCa = rol(Ebe, 1);
        Egi ^= Di; BCe = rol(Egi, 6);
        Eko ^= Do; BCi = rol(Eko, 25);
        Emu ^= Du; BCo = rol(Emu, 8);
        Esa ^= Da; BCu = rol(Esa, 18);
        Aka = BCa ^ ((~BCe) & BCi);
        Ake = BCe ^ ((~BCi) & BCo);
        Aki = BCi ^ ((~BCo) & BCu);
        Ako = BCo ^ ((~BCu) & BCa);
        Aku = BCu ^ ((~BCa) & BCe);

        Ebu ^= Du; BCa = rol(Ebu, 27);
        Ega ^= Da; BCe = rol(Ega, 36);
        Eke ^= De; BCi = rol(Eke, 10);
        Emi ^= Di; BCo = rol(Emi, 15);
        Eso ^= Do; BCu = rol(Eso, 56);
        Ama = BCa ^ ((~BCe) & BCi);
        Ame = BCe ^ ((~BCi) & BCo);
        Ami = BCi ^ ((~BCo) & BCu);
        Amo = BCo ^ ((~BCu) & BCa);
        Amu = BCu ^ ((~BCa) & BCe);

        Ebi ^= Di; BCa = rol(Ebi, 62);
        Ego ^= Do; BCe = rol(Ego, 55);
        Eku ^= Du; BCi = rol(Eku, 39);
        Ema ^= Da; BCo = rol(Ema, 41);
        Ese ^= De; BCu = rol(Ese, 2);
        Asa = BCa ^ ((~BCe) & BCi);
        Ase = BCe ^ ((~BCi) & BCo);
        Asi = BCi ^ ((~BCo) & BCu);
        Aso = BCo ^ ((~BCu) & BCa);
        Asu = BCu ^ ((~BCa) & BCe);
    }

    state[0] = Aba;  state[1] = Abe;  state[2] = Abi;  state[3] = Abo;  state[4] = Abu;
    state[5] = Aga;  state[6] = Age;  state[7] = Agi;  state[8] = Ago;  state[9] = Agu;
    state[10] = Aka; state[11] = Ake; state[12] = Aki; state[13] = Ako; state[14] = Aku;
    state[15] = Ama; state[16] = Ame; state[17] = Ami; state[18] = Amo; state[19] = Amu;
    state[20] = Asa; state[21] = Ase; state[22] = Asi; state[23] = Aso; state[24] = Asu;
}

// sponge 에 바이트 단위로 흡수 (증분 호출용)
unsigned int keccakAbsorb(uint64_t s[25], unsigned int pos, unsigned int r,
                          const uint8_t* in, size_t inlen) {
    unsigned int i;
    while (pos + inlen >= r) {
        for (i = pos; i < r; i++)
            s[i / 8] ^= static_cast<uint64_t>(*in++) << (8 * (i % 8));
        inlen -= r - pos;
        keccakF1600(s);
        pos = 0;
    }
    for (i = pos; i < pos + inlen; i++)
        s[i / 8] ^= static_cast<uint64_t>(*in++) << (8 * (i % 8));
    return i;
}

// 패딩 + 마지막 블록 마크
void keccakFinalize(uint64_t s[25], unsigned int pos, unsigned int r, uint8_t p) {
    s[pos / 8] ^= static_cast<uint64_t>(p) << (8 * (pos % 8));
    s[r / 8 - 1] ^= 1ULL << 63;
}

// sponge 에서 바이트 단위로 추출 (증분 호출용)
unsigned int keccakSqueeze(uint8_t* out, size_t outlen, uint64_t s[25],
                           unsigned int pos, unsigned int r) {
    unsigned int i;
    while (outlen) {
        if (pos == r) {
            keccakF1600(s);
            pos = 0;
        }
        for (i = pos; i < r && i < pos + outlen; i++)
            *out++ = static_cast<uint8_t>(s[i / 8] >> (8 * (i % 8)));
        outlen -= i - pos;
        pos = i;
    }
    return pos;
}

// 한 번에 흡수 + 패딩 (비-증분용)
void keccakAbsorbOnce(uint64_t s[25], unsigned int r,
                      const uint8_t* in, size_t inlen, uint8_t p) {
    unsigned int i;
    for (i = 0; i < 25; i++) s[i] = 0;
    while (inlen >= r) {
        for (i = 0; i < r / 8; i++)
            s[i] ^= load64(in + 8 * i);
        in += r;
        inlen -= r;
        keccakF1600(s);
    }
    for (i = 0; i < inlen; i++)
        s[i / 8] ^= static_cast<uint64_t>(in[i]) << (8 * (i % 8));
    s[i / 8] ^= static_cast<uint64_t>(p) << (8 * (i % 8));
    s[(r - 1) / 8] ^= 1ULL << 63;
}

// 블록 단위 추출 (비-증분용 최적화)
void keccakSqueezeBlocks(uint8_t* out, size_t nblocks, uint64_t s[25], unsigned int r) {
    unsigned int i;
    while (nblocks) {
        keccakF1600(s);
        for (i = 0; i < r / 8; i++)
            store64(out + 8 * i, s[i]);
        out += r;
        nblocks -= 1;
    }
}

} // anonymous namespace

// ============================================================================
// Sha3Base 템플릿 구현
// ============================================================================

template<size_t DigestSize, size_t Rate>
Sha3Base<DigestSize, Rate>::Sha3Base() {
    reset();
}

template<size_t DigestSize, size_t Rate>
void Sha3Base<DigestSize, Rate>::reset() {
    std::memset(s_, 0, sizeof(s_));
    pos_ = 0;
    finalized_ = false;
}

template<size_t DigestSize, size_t Rate>
void Sha3Base<DigestSize, Rate>::update(const uint8_t* data, size_t len) {
    if (finalized_ || len == 0) return;
    pos_ = keccakAbsorb(s_, pos_, Rate, data, len);
}

template<size_t DigestSize, size_t Rate>
void Sha3Base<DigestSize, Rate>::update(const std::vector<uint8_t>& data) {
    update(data.data(), data.size());
}

template<size_t DigestSize, size_t Rate>
typename Sha3Base<DigestSize, Rate>::Digest Sha3Base<DigestSize, Rate>::finalize() {
    if (finalized_) return Digest{};
    finalized_ = true;

    // SHA3 도메인 구분자 0x06 으로 패딩
    keccakFinalize(s_, pos_, Rate, 0x06);
    keccakF1600(s_);

    Digest digest{};
    for (unsigned int i = 0; i < DigestSize / 8; i++)
        store64(digest.data() + 8 * i, s_[i]);

    return digest;
}

template<size_t DigestSize, size_t Rate>
typename Sha3Base<DigestSize, Rate>::Digest
Sha3Base<DigestSize, Rate>::hash(const uint8_t* data, size_t len) {
    uint64_t s[25];
    keccakAbsorbOnce(s, Rate, data, len, 0x06);
    keccakF1600(s);

    Digest digest{};
    for (unsigned int i = 0; i < DigestSize / 8; i++)
        store64(digest.data() + 8 * i, s[i]);

    return digest;
}

template<size_t DigestSize, size_t Rate>
typename Sha3Base<DigestSize, Rate>::Digest
Sha3Base<DigestSize, Rate>::hash(const std::vector<uint8_t>& data) {
    return hash(data.data(), data.size());
}

// 명시적 인스턴스화
template class Sha3Base<32, 136>;  // SHA3-256
template class Sha3Base<64, 72>;   // SHA3-512

// ============================================================================
// ShakeBase 템플릿 구현
// ============================================================================

template<size_t Rate>
ShakeBase<Rate>::ShakeBase() {
    reset();
}

template<size_t Rate>
void ShakeBase<Rate>::reset() {
    std::memset(s_, 0, sizeof(s_));
    pos_ = 0;
    absorbed_ = false;
}

template<size_t Rate>
void ShakeBase<Rate>::absorb(const uint8_t* data, size_t len) {
    if (absorbed_ || len == 0) return;
    pos_ = keccakAbsorb(s_, pos_, Rate, data, len);
}

template<size_t Rate>
void ShakeBase<Rate>::absorbOnce(const uint8_t* data, size_t len) {
    // SHAKE 도메인 구분자 0x1F
    keccakAbsorbOnce(s_, Rate, data, len, 0x1F);
    pos_ = Rate;
    absorbed_ = true;
}

template<size_t Rate>
void ShakeBase<Rate>::finalize() {
    if (absorbed_) return;
    absorbed_ = true;
    // SHAKE 도메인 구분자 0x1F
    keccakFinalize(s_, pos_, Rate, 0x1F);
    pos_ = Rate;
}

template<size_t Rate>
void ShakeBase<Rate>::squeeze(uint8_t* out, size_t outlen) {
    pos_ = keccakSqueeze(out, outlen, s_, pos_, Rate);
}

template<size_t Rate>
void ShakeBase<Rate>::hash(uint8_t* out, size_t outlen, const uint8_t* in, size_t inlen) {
    ShakeBase<Rate> xof;
    xof.absorbOnce(in, inlen);
    size_t nblocks = outlen / Rate;
    keccakSqueezeBlocks(out, nblocks, xof.s_, Rate);
    outlen -= nblocks * Rate;
    out += nblocks * Rate;
    xof.squeeze(out, outlen);
}

// 명시적 인스턴스화
template class ShakeBase<168>;  // SHAKE128
template class ShakeBase<136>;  // SHAKE256
