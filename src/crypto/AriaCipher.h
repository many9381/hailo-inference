#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ICipher.h"

// ----------------------------------------------------------------------------
// AriaCipher
//
// ARIA implementation of ICipher. Uses the KISA standard (KS X 1213:2004)
// ARIA block cipher.
// Following OpenSSL's small-footprint approach, it uses byte-level S-Boxes and
// a clearly separated substitution/diffusion structure.
// CTR mode is used for same-size in-place RTP payload encryption/decryption.
//
// - key: 16/24/32 bytes (ARIA-128/192/256). Default is 16 bytes (ARIA-128).
// - iv : 16 bytes (initial CTR counter value).
// ----------------------------------------------------------------------------
class AriaCipher : public ICipher {
public:
    static constexpr int kBlockSize = 16;
    static constexpr int kMaxRounds = 16;

    // 128-bit block represented as a byte array
    struct u128 {
        uint8_t c[kBlockSize]{};
    };

    // ARIA round-key struct
    struct AriaKey {
        u128 rd_key[kMaxRounds + 1]{};
        int  rounds = 0;
    };

    AriaCipher() = default;

    // Allowed key sizes: 16 (ARIA-128), 24 (ARIA-192), or 32 (ARIA-256) bytes only.
    bool setKey(const std::string& key) override;
    bool setKey(const std::vector<uint8_t>& key) override;

    // IV size: exactly 16 bytes only.
    bool setIv(const std::string& iv) override;
    bool setIv(const std::vector<uint8_t>& iv) override;

    // CTR mode: encrypt == decrypt (same operation)
    bool encrypt(uint8_t* data, size_t size) const override;
    bool decrypt(uint8_t* data, size_t size) const override;

    bool hasKey() const override { return keyInitialized_; }

private:
    void initKey();
    void processBlock(const uint8_t* in, uint8_t* out) const;
    void ctrTransform(uint8_t* data, size_t size) const;

    // ── State ────────────────────────────────────────────────────────────
    std::vector<uint8_t> key_;
    std::vector<uint8_t> iv_;
    AriaKey              encKey_{};
    bool                 keyInitialized_ = false;
};
