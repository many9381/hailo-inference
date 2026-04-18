#pragma once

#include "hmac/Hmac.h"
#include "sha/Sha1.h"

// ----------------------------------------------------------------------------
// HmacSha1
//
// HMAC-SHA1 구현. Hmac<Sha1> 을 래핑하여 편의 인터페이스를 제공한다.
// RFC 2104 + RFC 3174 조합으로, SRTP 인증 태그 등에 사용된다.
//
// 사용법:
//   HmacSha1 mac;
//   mac.setKey(key, keyLen);
//   mac.update(data, dataLen);
//   auto tag = mac.finalize();
//
//   // 또는 단일 호출:
//   auto tag = HmacSha1::compute(key, keyLen, data, dataLen);
// ----------------------------------------------------------------------------
class HmacSha1 {
public:
    static constexpr size_t kDigestSize = Sha1::kDigestSize;  // 20 바이트
    static constexpr size_t kBlockSize  = Sha1::kBlockSize;   // 64 바이트

    using Digest = Sha1::Digest;

    HmacSha1() = default;

    // 키 설정
    void setKey(const uint8_t* key, size_t keyLen);
    void setKey(const std::vector<uint8_t>& key);

    // 데이터 추가
    void update(const uint8_t* data, size_t len);
    void update(const std::vector<uint8_t>& data);

    // MAC 완료 — 20 바이트 다이제스트 반환
    Digest finalize();

    // 상태 초기화 (동일 키로 재사용)
    void reset();

    // 편의 함수: 단일 호출로 HMAC-SHA1 계산
    static Digest compute(const uint8_t* key, size_t keyLen,
                          const uint8_t* data, size_t dataLen);
    static Digest compute(const std::vector<uint8_t>& key,
                          const std::vector<uint8_t>& data);

private:
    Hmac<Sha1> hmac_;
};
