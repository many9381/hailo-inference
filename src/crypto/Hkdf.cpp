#include "Hkdf.h"
#include "hmac/HmacSha256.h"

#include <algorithm>
#include <cstring>

// ============================================================================
// HKDF-Extract (RFC 5869 Section 2.2)
//
// PRK = HMAC-SHA256(salt, IKM)
// ============================================================================

std::vector<uint8_t> Hkdf::extract(const std::vector<uint8_t>& salt,
                                    const std::vector<uint8_t>& ikm) {
    return extract(salt.data(), salt.size(), ikm.data(), ikm.size());
}

std::vector<uint8_t> Hkdf::extract(const uint8_t* salt, size_t saltLen,
                                    const uint8_t* ikm, size_t ikmLen) {
    // salt 이 비어 있으면 HashLen 크기의 0 배열을 사용
    std::vector<uint8_t> defaultSalt;
    const uint8_t* actualSalt = salt;
    size_t actualSaltLen = saltLen;

    if (saltLen == 0 || salt == nullptr) {
        defaultSalt.resize(HmacSha256::kDigestSize, 0);
        actualSalt = defaultSalt.data();
        actualSaltLen = defaultSalt.size();
    }

    auto prk = HmacSha256::compute(actualSalt, actualSaltLen, ikm, ikmLen);
    return std::vector<uint8_t>(prk.begin(), prk.end());
}

// ============================================================================
// HKDF-Expand (RFC 5869 Section 2.3)
//
// N = ceil(L / HashLen)
// T(0) = empty
// T(i) = HMAC-SHA256(PRK, T(i-1) || info || i)   (i = 1..N)
// OKM  = first L octets of T(1) || T(2) || ... || T(N)
// ============================================================================

std::vector<uint8_t> Hkdf::expand(const std::vector<uint8_t>& prk,
                                   const std::vector<uint8_t>& info,
                                   size_t outputLen) {
    return expand(prk.data(), prk.size(), info.data(), info.size(), outputLen);
}

std::vector<uint8_t> Hkdf::expand(const uint8_t* prk, size_t prkLen,
                                   const uint8_t* info, size_t infoLen,
                                   size_t outputLen) {
    const size_t hashLen = HmacSha256::kDigestSize;
    size_t n = (outputLen + hashLen - 1) / hashLen;
    if (n > 255) n = 255;  // RFC 5869 제한

    std::vector<uint8_t> okm;
    okm.reserve(outputLen);

    std::vector<uint8_t> prev;  // T(i-1), 처음에는 비어 있음

    for (size_t i = 1; i <= n; ++i) {
        HmacSha256 hmac;
        hmac.setKey(prk, prkLen);
        if (!prev.empty()) {
            hmac.update(prev.data(), prev.size());
        }
        if (infoLen > 0) {
            hmac.update(info, infoLen);
        }
        uint8_t counter = static_cast<uint8_t>(i);
        hmac.update(&counter, 1);

        auto t = hmac.finalize();
        prev.assign(t.begin(), t.end());

        size_t remaining = outputLen - okm.size();
        size_t toCopy = std::min(hashLen, remaining);
        okm.insert(okm.end(), t.begin(), t.begin() + toCopy);
    }

    return okm;
}

// ============================================================================
// deriveKey — extract + expand 편의 함수
// ============================================================================

std::vector<uint8_t> Hkdf::deriveKey(const std::vector<uint8_t>& salt,
                                      const std::vector<uint8_t>& ikm,
                                      const std::vector<uint8_t>& info,
                                      size_t outputLen) {
    auto prk = extract(salt, ikm);
    return expand(prk, info, outputLen);
}
