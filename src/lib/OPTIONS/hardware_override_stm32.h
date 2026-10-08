#pragma once

/*
 * hardware_override_stm32 — the hardware layout override (STM32 only)
 * ===================================================================
 *
 * The ESP targets keep a user-edited layout as /hardware.json in LittleFS. STM32 has no
 * filesystem, so the override document is stored in the config flash (W25Q64), one sector after
 * the elrs_eeprom blob:
 *
 *   0x000000  sector 0   elrs_eeprom blob (lib/elrs_eeprom/elrs_eeprom.cpp)
 *   0x001000  sector 1   hw_override_header_t + `len` bytes of layout JSON
 *
 * Validity rules (all must hold, or the override is ignored and the flashed layout is used):
 *   - magic == 'T','L','H','W' and version == HW_OVERRIDE_VERSION
 *   - 1 <= len <= ELRSOPTS_HARDWARE_SIZE
 *   - crc32 matches the payload
 *   - discriminator == fw_options_discriminator(), which folds in the flash-discriminator of the
 *     web flash, so reflashing drops the override while reboots and power cycles keep it.
 *
 * An override replaces the flashed layout entirely, except `config_flash_*`, which always come
 * from the flashed layout — the override is only reachable through those pins.
 */

#include <stdint.h>
#include <stddef.h>

#define HW_OVERRIDE_FLASH_ADDR 0x001000UL
#define HW_OVERRIDE_VERSION    1

struct hw_override_header_t {
    uint8_t  magic[4];        // 'T','L','H','W'
    uint16_t version;         // HW_OVERRIDE_VERSION
    uint16_t len;             // JSON byte length, 1..ELRSOPTS_HARDWARE_SIZE
    uint32_t discriminator;   // fw_options_discriminator() at save time
    uint32_t crc32;           // CRC-32 of the JSON payload
} __attribute__((packed));

// Pure validity check of a header + payload against the running discriminator (hardware.cpp).
bool hwOverride_IsValid(const hw_override_header_t &header, const uint8_t *payload, uint32_t discriminator);

#if defined(PLATFORM_STM32)
#include <Arduino.h>

// Loads a valid override for the current discriminator into `out`. False if there is none.
bool hwOverride_Load(String &out);
// Erases the override sector and writes `json` with the current discriminator.
bool hwOverride_Save(const char *json, size_t len);
// Erases the override sector.
void hwOverride_Clear();
#endif
