#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

// ----------------------------------------------------------------------------
// Sha1
//
// SHA-1 해시 알고리즘 구현 (RFC 3174).
// 160 비트(20 바이트) 해시 값을 생성한다.
//
// 사용법:
//   Sha1 sha;
//   sha.update(data, len);
//   sha.update(moreData, moreLen);
//   auto digest = sha.finalize();
//
// finalize() 호출 후에는 reset() 을 호출해야 재사용 가능하다.
// ----------------------------------------------------------------------------
class Sha1 {
public:
    static constexpr size_t kDigestSize = 20;   // 160 bit
    static constexpr size_t kBlockSize  = 64;   // 512 bit

    using Digest = std::array<uint8_t, kDigestSize>;

    Sha1();

    // 데이터 추가
    void update(const uint8_t* data, size_t len);
    void update(const std::vector<uint8_t>& data);

    // 해시 완료 — 패딩 적용 후 최종 다이제스트 반환
    Digest finalize();

    // 상태 초기화 (재사용)
    void reset();

    // 편의 함수: 단일 버퍼 해시
    static Digest hash(const uint8_t* data, size_t len);
    static Digest hash(const std::vector<uint8_t>& data);

private:
    void processBlock(const uint8_t block[kBlockSize]);

    // SHA-1 상태 (h0–h4)
    uint32_t h_[5]{};

    // 입력 버퍼 (64 바이트 블록 미만 잔여 데이터)
    uint8_t  buffer_[kBlockSize]{};
    size_t   bufferLen_ = 0;

    // 총 입력 바이트 수
    uint64_t totalLen_  = 0;

    // finalize 호출 여부
    bool     finalized_ = false;
};
