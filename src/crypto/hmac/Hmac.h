#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// ----------------------------------------------------------------------------
// Hmac<HashT>
//
// RFC 2104 HMAC 알고리즘의 템플릿 구현.
// HashT 는 다음 인터페이스를 제공해야 한다:
//
//   static constexpr size_t kDigestSize;  // 해시 출력 바이트 수
//   static constexpr size_t kBlockSize;   // 해시 블록 바이트 수
//   using Digest = std::array<uint8_t, kDigestSize>;
//
//   void reset();
//   void update(const uint8_t* data, size_t len);
//   Digest finalize();
//   static Digest hash(const uint8_t* data, size_t len);
//
// 사용법:
//   Hmac<Sha1> hmac;
//   hmac.setKey(key, keyLen);
//   hmac.update(data, dataLen);
//   auto mac = hmac.finalize();
// ----------------------------------------------------------------------------
template <typename HashT>
class Hmac {
public:
    static constexpr size_t kDigestSize = HashT::kDigestSize;
    static constexpr size_t kBlockSize  = HashT::kBlockSize;

    using Digest = typename HashT::Digest;

    Hmac() = default;

    // 키 설정 — 내부적으로 ipad/opad 를 준비하고 inner hash 를 초기화한다
    void setKey(const uint8_t* key, size_t keyLen);
    void setKey(const std::vector<uint8_t>& key);

    // 데이터 추가 (inner hash 에 전달)
    void update(const uint8_t* data, size_t len);
    void update(const std::vector<uint8_t>& data);

    // MAC 완료 — inner hash finalize → outer hash 적용 → 최종 다이제스트 반환
    Digest finalize();

    // 상태 초기화 (동일 키로 재사용)
    void reset();

    // 편의 함수: 단일 호출로 HMAC 계산
    static Digest compute(const uint8_t* key, size_t keyLen,
                          const uint8_t* data, size_t dataLen);
    static Digest compute(const std::vector<uint8_t>& key,
                          const std::vector<uint8_t>& data);

private:
    uint8_t ipad_[kBlockSize]{};
    uint8_t opad_[kBlockSize]{};

    HashT   innerHash_;
    bool    keySet_    = false;
    bool    finalized_ = false;
};

// ============================================================================
// 템플릿 구현
// ============================================================================

template <typename HashT>
void Hmac<HashT>::setKey(const uint8_t* key, size_t keyLen) {
    uint8_t keyBlock[kBlockSize]{};

    // 키가 블록 크기보다 크면 해시하여 축소
    if (keyLen > kBlockSize) {
        auto hashed = HashT::hash(key, keyLen);
        std::memcpy(keyBlock, hashed.data(), kDigestSize);
    } else {
        std::memcpy(keyBlock, key, keyLen);
    }

    // ipad = keyBlock XOR 0x36, opad = keyBlock XOR 0x5c
    for (size_t i = 0; i < kBlockSize; ++i) {
        ipad_[i] = keyBlock[i] ^ 0x36;
        opad_[i] = keyBlock[i] ^ 0x5C;
    }

    keySet_    = true;
    finalized_ = false;

    // inner hash 초기화: H(ipad || ...)
    innerHash_.reset();
    innerHash_.update(ipad_, kBlockSize);
}

template <typename HashT>
void Hmac<HashT>::setKey(const std::vector<uint8_t>& key) {
    setKey(key.data(), key.size());
}

template <typename HashT>
void Hmac<HashT>::update(const uint8_t* data, size_t len) {
    if (!keySet_ || finalized_) return;
    innerHash_.update(data, len);
}

template <typename HashT>
void Hmac<HashT>::update(const std::vector<uint8_t>& data) {
    update(data.data(), data.size());
}

template <typename HashT>
typename Hmac<HashT>::Digest Hmac<HashT>::finalize() {
    if (!keySet_) return Digest{};
    if (finalized_) {
        // 이미 완료된 경우 다시 계산하지 않고 캐시된 결과 반환은 하지 않음
        // reset 후 재사용해야 한다
        return Digest{};
    }

    finalized_ = true;

    // inner = H(ipad || message)
    Digest inner = innerHash_.finalize();

    // outer = H(opad || inner)
    HashT outerHash;
    outerHash.update(opad_, kBlockSize);
    outerHash.update(inner.data(), kDigestSize);
    return outerHash.finalize();
}

template <typename HashT>
void Hmac<HashT>::reset() {
    if (!keySet_) return;

    finalized_ = false;
    innerHash_.reset();
    innerHash_.update(ipad_, kBlockSize);
}

template <typename HashT>
typename Hmac<HashT>::Digest Hmac<HashT>::compute(
    const uint8_t* key, size_t keyLen,
    const uint8_t* data, size_t dataLen) {
    Hmac<HashT> hmac;
    hmac.setKey(key, keyLen);
    hmac.update(data, dataLen);
    return hmac.finalize();
}

template <typename HashT>
typename Hmac<HashT>::Digest Hmac<HashT>::compute(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& data) {
    return compute(key.data(), key.size(), data.data(), data.size());
}
