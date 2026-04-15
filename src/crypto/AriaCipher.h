#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ICipher.h"

// ----------------------------------------------------------------------------
// AriaCipher
//
// ICipher 의 ARIA 구현체. KISA 표준(KS X 1213:2004) ARIA 블록 암호를 사용한다.
// OpenSSL 의 small-footprint 구현 방식을 참고하여 바이트 단위 S-Box 와
// 명확한 치환/확산 함수 분리 구조를 사용한다.
// RTP payload 의 in-place 동일 크기 암복호화를 위해 CTR 모드를 사용한다.
//
// - key: 16/24/32 바이트 (ARIA-128/192/256). 기본 16 바이트(ARIA-128).
// - iv : 16 바이트 (CTR 카운터 초기값).
// ----------------------------------------------------------------------------
class AriaCipher : public ICipher {
public:
    static constexpr int kBlockSize = 16;
    static constexpr int kMaxRounds = 16;

    // 128 비트 블록을 바이트 배열로 표현
    struct u128 {
        uint8_t c[kBlockSize]{};
    };

    // ARIA 라운드 키 구조체
    struct AriaKey {
        u128 rd_key[kMaxRounds + 1]{};
        int  rounds = 0;
    };

    AriaCipher() = default;
    explicit AriaCipher(const std::string& key);
    explicit AriaCipher(const std::vector<uint8_t>& key);

    void setKey(const std::string& key) override;
    void setKey(const std::vector<uint8_t>& key) override;

    void setIv(const std::string& iv) override;
    void setIv(const std::vector<uint8_t>& iv) override;

    // CTR 모드: encrypt == decrypt (동일 연산)
    void encrypt(uint8_t* data, size_t size) const override;
    void decrypt(uint8_t* data, size_t size) const override;

    bool hasKey() const override { return keyInitialized_; }

private:
    void initKey();
    void processBlock(const uint8_t* in, uint8_t* out) const;
    void ctrTransform(uint8_t* data, size_t size) const;

    // ── 상태 ─────────────────────────────────────────────────────────────
    std::vector<uint8_t> key_;
    std::vector<uint8_t> iv_;
    AriaKey              encKey_{};
    bool                 keyInitialized_ = false;
};
