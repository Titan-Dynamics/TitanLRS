#include "elrs_eeprom.h"
#include "targets.h"
#include "logging.h"

#if !defined(TARGET_NATIVE)
#if defined(PLATFORM_STM32)
    #if defined(HAS_W25Q64_CONFIG)
        #include "W25Q64.h"

        static W25Q64 s_flash(W25Q64_CS_PIN, W25Q64_SCK_PIN, W25Q64_MISO_PIN, W25Q64_MOSI_PIN);
        static uint8_t s_storage[RESERVED_EEPROM_SIZE];

        // Sector 0 layout: ['T','L','R','S'] [len:u16 LE] [blob:len] [crc32:u32 LE]
        static const uint32_t W25Q64_CONFIG_ADDR  = 0x000000;
        static const uint8_t  W25Q64_MAGIC[4]     = { 'T', 'L', 'R', 'S' };

        static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, uint32_t len)
        {
            crc = ~crc;
            for (uint32_t i = 0; i < len; i++)
            {
                crc ^= buf[i];
                for (uint8_t b = 0; b < 8; b++)
                {
                    crc = (crc >> 1) ^ (0xEDB88320UL & -(int32_t)(crc & 1));
                }
            }
            return ~crc;
        }
    #elif defined(TARGET_USE_EEPROM) && defined(USE_I2C)
        #if !defined(TARGET_EEPROM_ADDR)
            #define TARGET_EEPROM_ADDR 0x51
            #warning "!! Using default EEPROM address (0x51) !!"
        #endif

        #include <Wire.h>
        #include <extEEPROM.h>
        extEEPROM EEPROM(kbits_2, 1, 1, TARGET_EEPROM_ADDR);
    #else
        #define STM32_USE_FLASH
        #include <utility/stm32_eeprom.h>
    #endif
#else
    #include <EEPROM.h>
#endif

void
ELRS_EEPROM::Begin()
{
#if defined(PLATFORM_STM32)
    #if defined(HAS_W25Q64_CONFIG)
        if (!s_flash.begin())
        {
            ERRLN("W25Q64 not found - using defaults");
            memset(s_storage, 0, sizeof(s_storage));
            return;
        }

        uint8_t header[6];
        s_flash.read(W25Q64_CONFIG_ADDR, header, sizeof(header));

        if (memcmp(header, W25Q64_MAGIC, 4) != 0)
        {
            DBGLN("W25Q64 config: no magic, using defaults");
            memset(s_storage, 0, sizeof(s_storage));
            return;
        }

        uint16_t len = (uint16_t)header[4] | ((uint16_t)header[5] << 8);
        if (len != RESERVED_EEPROM_SIZE)
        {
            DBGLN("W25Q64 config: len mismatch (%u), using defaults", len);
            memset(s_storage, 0, sizeof(s_storage));
            return;
        }

        s_flash.read(W25Q64_CONFIG_ADDR + 6, s_storage, RESERVED_EEPROM_SIZE);

        uint8_t crc_bytes[4];
        s_flash.read(W25Q64_CONFIG_ADDR + 6 + RESERVED_EEPROM_SIZE, crc_bytes, 4);
        uint32_t crc_stored = (uint32_t)crc_bytes[0]
                            | ((uint32_t)crc_bytes[1] << 8)
                            | ((uint32_t)crc_bytes[2] << 16)
                            | ((uint32_t)crc_bytes[3] << 24);

        uint32_t crc = 0;
        crc = crc32_update(crc, W25Q64_MAGIC, 4);
        crc = crc32_update(crc, &header[4], 2);
        crc = crc32_update(crc, s_storage, RESERVED_EEPROM_SIZE);
        if (crc != crc_stored)
        {
            DBGLN("W25Q64 config: CRC mismatch, using defaults");
            memset(s_storage, 0, sizeof(s_storage));
            return;
        }

        DBGLN("W25Q64 config loaded");
    #elif defined(STM32_USE_FLASH)
        eeprom_buffer_fill();
    #else // !STM32_USE_FLASH
        // I2C initialization is the responsibility of the caller
        #if defined(TARGET_EEPROM_400K)
            EEPROM.begin(extEEPROM::twiClock400kHz, &Wire);
        #else
            EEPROM.begin(extEEPROM::twiClock100kHz, &Wire);
        #endif
    #endif // HAS_W25Q64_CONFIG
#else /* !PLATFORM_STM32 */
    EEPROM.begin(RESERVED_EEPROM_SIZE);
#endif /* PLATFORM_STM32 */
}

uint8_t
ELRS_EEPROM::ReadByte(const uint32_t address)
{
    if (address >= RESERVED_EEPROM_SIZE)
    {
        // address is out of bounds
        ERRLN("EEPROM address is out of bounds");
        return 0;
    }
#if defined(HAS_W25Q64_CONFIG)
    return s_storage[address];
#elif defined(STM32_USE_FLASH)
    return eeprom_buffered_read_byte(address);
#else
    return EEPROM.read(address);
#endif
}

void
ELRS_EEPROM::WriteByte(const uint32_t address, const uint8_t value)
{
    if (address >= RESERVED_EEPROM_SIZE)
    {
        // address is out of bounds
        ERRLN("EEPROM address is out of bounds");
        return;
    }
#if defined(HAS_W25Q64_CONFIG)
    s_storage[address] = value;
#elif defined(STM32_USE_FLASH)
    eeprom_buffered_write_byte(address, value);
#elif defined(PLATFORM_STM32)
    EEPROM.update(address, value);
#else
    EEPROM.write(address, value);
#endif
}

void
ELRS_EEPROM::Commit()
{
#if defined(HAS_W25Q64_CONFIG)
    uint32_t t0 = micros();

    s_flash.sectorErase4K(W25Q64_CONFIG_ADDR);

    uint8_t header[6];
    memcpy(header, W25Q64_MAGIC, 4);
    header[4] = (uint8_t)(RESERVED_EEPROM_SIZE & 0xFF);
    header[5] = (uint8_t)((RESERVED_EEPROM_SIZE >> 8) & 0xFF);

    uint32_t crc = 0;
    crc = crc32_update(crc, W25Q64_MAGIC, 4);
    crc = crc32_update(crc, &header[4], 2);
    crc = crc32_update(crc, s_storage, RESERVED_EEPROM_SIZE);

    uint8_t crc_bytes[4] = {
        (uint8_t)(crc & 0xFF),
        (uint8_t)((crc >> 8) & 0xFF),
        (uint8_t)((crc >> 16) & 0xFF),
        (uint8_t)((crc >> 24) & 0xFF),
    };

    // First page: 6 header bytes + first (256-6)=250 blob bytes.
    uint8_t first[256];
    memcpy(first, header, 6);
    memcpy(first + 6, s_storage, 250);
    s_flash.pageProgram(W25Q64_CONFIG_ADDR, first, 256);

    // Remaining blob bytes in 256-byte pages.
    uint32_t written = 250;
    uint32_t addr = W25Q64_CONFIG_ADDR + 256;
    while (written < RESERVED_EEPROM_SIZE)
    {
        uint32_t chunk = RESERVED_EEPROM_SIZE - written;
        if (chunk > 256) chunk = 256;
        s_flash.pageProgram(addr, &s_storage[written], chunk);
        written += chunk;
        addr += chunk;
    }

    // CRC tail (4 bytes) — sits at offset 6 + RESERVED_EEPROM_SIZE within
    // the same sector. With RESERVED_EEPROM_SIZE=1024 that's offset 1030,
    // inside sector 0 (4 KB), and inside a 256-byte page boundary.
    s_flash.pageProgram(W25Q64_CONFIG_ADDR + 6 + RESERVED_EEPROM_SIZE, crc_bytes, 4);

    DBGLN("W25Q64 commit %u us", (unsigned)(micros() - t0));
#elif defined(PLATFORM_ESP32) || defined(PLATFORM_ESP8266)
    if (!EEPROM.commit())
    {
      ERRLN("EEPROM commit failed");
    }
#elif defined(STM32_USE_FLASH)
    eeprom_buffer_flush();
#endif
  // PLATFORM_STM32 with external flash every byte is committed as it is written
}

#endif /* !TARGET_NATIVE */
