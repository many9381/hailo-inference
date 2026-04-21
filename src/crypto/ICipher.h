#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// ICipher
//
// Interface for encrypting and decrypting RTP payloads.
// Concrete algorithms (XOR, AES, etc.) are provided by implementing this interface.
// Callers only hold an ICipher pointer, so swapping algorithms
// only requires changing the construction site.
// ----------------------------------------------------------------------------
class ICipher {
public:
    virtual ~ICipher() = default;

    // Set the key. Returns false and does not store the key if the size is invalid for the algorithm.
    virtual bool setKey(const std::string& key) = 0;
    virtual bool setKey(const std::vector<uint8_t>& key) = 0;

    // Set the IV. Returns false and does not store the IV if the size is invalid for the algorithm.
    virtual bool setIv(const std::string& iv) = 0;
    virtual bool setIv(const std::vector<uint8_t>& iv) = 0;

    // In-place encryption. Returns false if the key/IV has not been set.
    virtual bool encrypt(uint8_t* data, size_t size) const = 0;

    // In-place decryption. Returns false if the key/IV has not been set.
    virtual bool decrypt(uint8_t* data, size_t size) const = 0;

    // Whether the key is set
    virtual bool hasKey() const = 0;
};
