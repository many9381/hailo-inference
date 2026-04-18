#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// ----------------------------------------------------------------------------
// Hkdf
//
// HKDF-SHA256 구현 (RFC 5869).
// TLS 1.3 핸드셰이크에서 공유 비밀로부터 키 재료를 유도하는 데 사용된다.
//
// 사용법:
//   auto prk  = Hkdf::extract(salt, ikm);
//   auto keys = Hkdf::expand(prk, info, outputLen);
// ----------------------------------------------------------------------------
class Hkdf {
public:
    // HKDF-Extract: salt 과 입력 키 재료(IKM) 에서 의사 난수 키(PRK) 를 추출한다.
    // salt 이 비어 있으면 SHA-256 해시 크기(32 바이트) 의 0 바이트 배열을 사용한다.
    static std::vector<uint8_t> extract(const std::vector<uint8_t>& salt,
                                        const std::vector<uint8_t>& ikm);

    static std::vector<uint8_t> extract(const uint8_t* salt, size_t saltLen,
                                        const uint8_t* ikm, size_t ikmLen);

    // HKDF-Expand: PRK 와 컨텍스트 정보(info) 에서 원하는 길이의 출력 키 재료(OKM) 를 생성한다.
    // outputLen 은 최대 255 * 32 = 8160 바이트까지 가능하다.
    static std::vector<uint8_t> expand(const std::vector<uint8_t>& prk,
                                       const std::vector<uint8_t>& info,
                                       size_t outputLen);

    static std::vector<uint8_t> expand(const uint8_t* prk, size_t prkLen,
                                       const uint8_t* info, size_t infoLen,
                                       size_t outputLen);

    // 편의 함수: extract + expand 를 한 번에 수행
    static std::vector<uint8_t> deriveKey(const std::vector<uint8_t>& salt,
                                          const std::vector<uint8_t>& ikm,
                                          const std::vector<uint8_t>& info,
                                          size_t outputLen);
};
