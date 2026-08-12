#pragma once

/*
 * options_storage_stm32 — on-flash layout for the persisted firmware_options_t (STM32 only)
 * =========================================================================================
 *
 * The ESP targets persist firmwareOptions as /options.json in LittleFS. STM32 has no
 * filesystem, so the struct is stored raw in the existing `elrs_eeprom` blob (W25Q64 sector 0
 * on every one of our targets), above the region used by TxConfig / RxConfig.
 *
 * elrs_eeprom RAM mirror layout (RESERVED_EEPROM_SIZE == 1024):
 *
 *     0 .............................. 511   tx_config_t / rx_config_t   (config.cpp Get/Put(0,...))
 *   512 .. 512+sizeof(hdr) ................   fw_options_header_t
 *        .. +sizeof(firmware_options_t) ..    firmwareOptions payload
 *        .............................. 1023 unused
 *
 * RESERVED_EEPROM_SIZE MUST STAY 1024: ELRS_EEPROM::Begin() rejects a stored blob whose
 * recorded length differs from it (elrs_eeprom.cpp), so growing it would silently wipe the
 * config of every device already in the field. The two static_asserts that police this layout
 * live next to the structures they constrain — the config-size one in
 * lib/ConfigJson/config_json.cpp (which sees the config struct), the options-size one in
 * lib/OPTIONS/options.cpp.
 *
 * Header validity rules (all must hold, or the blob is discarded and compile-time defaults are
 * re-seeded):
 *   - magic   == FW_OPTIONS_MAGIC
 *   - version == FW_OPTIONS_VERSION
 *   - size    == sizeof(firmware_options_t) at build time. firmware_options_t is packed and
 *               differs between TX and RX and across upstream rebases; this makes a layout
 *               change self-invalidating instead of loading garbage.
 *   - crc matches (CRC16/CCITT-FALSE over the header bytes preceding `crc`, then the payload —
 *               so the `customised` flag is covered by the CRC too).
 *   - the payload's flash_discriminator equals the compile-time one (MY_UID + LATEST_COMMIT),
 *               i.e. re-flashing with a different binding phrase or firmware drops stored
 *               overrides, matching the ESP flash-discriminator contract.
 */

#include <stdint.h>
#include <stddef.h>

#define FW_OPTIONS_EEPROM_OFFSET  512
#define FW_OPTIONS_MAGIC          0xEF
#define FW_OPTIONS_VERSION        1

struct fw_options_header_t {
    uint8_t  magic;
    uint8_t  version;
    uint16_t size;          // sizeof(firmware_options_t) at build time
    uint8_t  customised;    // 1 once anything has been written over USB (reported as options.customised)
    uint8_t  _reserved;
    uint16_t crc;           // CRC16/CCITT-FALSE over the 6 bytes above + the payload
} __attribute__((packed));
