#ifndef CRC32_UTIL_H
#define CRC32_UTIL_H

#include <stddef.h>
#include <stdint.h>

static inline uint32_t crc32_init(void) {
    return 0xFFFFFFFFu;
}

static inline uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    while (len--) {
        crc ^= (uint32_t)(*data++);
        for (uint32_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static inline uint32_t crc32_finalize(uint32_t crc) {
    return crc ^ 0xFFFFFFFFu;
}

#endif
