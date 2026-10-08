#pragma once

#include <stdint.h>
#include <cstddef>

#define RESERVED_EEPROM_SIZE 1024

// CRC-32 (IEEE, reflected), continuing from `crc` (0 to start).
uint32_t elrs_crc32_update(uint32_t crc, const uint8_t *buf, uint32_t len);

#if defined(HAS_W25Q64_CONFIG)
class W25Q64;
// The config flash brought up by ELRS_EEPROM::Begin(), or nullptr when the target has no
// config-flash pins or the chip did not answer.
W25Q64 *elrs_ConfigFlash();
#endif

class ELRS_EEPROM
{
public:
    void Begin();
    uint8_t ReadByte(const uint32_t address);
    void WriteByte(const uint32_t address, const uint8_t value);
    void Commit();

    // The extEEPROM lib that we use for STM doesn't have the get and put templates
    // These templates need to be reimplemented here
    template <typename T> void Get(uint32_t addr, T &value)
    {
        uint8_t* p = (uint8_t*)(void*)&value;
        size_t   i = sizeof(value);
        while(i--)  *p++ = ReadByte(addr++);
    };

    template <typename T> const void Put(uint32_t addr, const T &value)
    {
        const uint8_t* p = (const uint8_t*)(const void*)&value;
        size_t         i = sizeof(value);
        while(i--)  WriteByte(addr++, *p++);
    };
};
