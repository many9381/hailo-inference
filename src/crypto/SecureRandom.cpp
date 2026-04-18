#include "SecureRandom.h"

#ifdef __APPLE__
#include <stdlib.h>  // arc4random_buf
#else
#include <cstdio>
#endif

bool SystemRandom::generate(uint8_t* buf, size_t size) {
    if (size == 0) return true;

#ifdef __APPLE__
    arc4random_buf(buf, size);
    return true;
#else
    // Linux: /dev/urandom
    FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) return false;
    size_t read = std::fread(buf, 1, size, f);
    std::fclose(f);
    return read == size;
#endif
}

std::vector<uint8_t> SystemRandom::generate(size_t size) {
    std::vector<uint8_t> buf(size);
    if (!generate(buf.data(), size)) {
        buf.clear();
    }
    return buf;
}
