#pragma once

#include "targets.h"

extern const unsigned char target_name[];
extern const uint8_t target_name_size;
extern const char commit[];
extern const char version[];

extern const char *wifi_hostname;
extern const char *wifi_ap_ssid;
extern const char *wifi_ap_password;
extern const char *wifi_ap_address;

enum BuzzerMode {
    buzzerQuiet,
    buzzerOne,
    buzzerTune
};

typedef struct _options {
    uint8_t     _magic_[8];     // this is the magic constant so the configurator can find this options block
    uint16_t    _version_;      // the version of this structure
    uint8_t     domain;         // depends on radio chip
    uint8_t     hasUID;
    uint8_t     uid[6];         // MY_UID derived from MY_BINDING_PHRASE
    uint32_t    flash_discriminator;    // Discriminator value used to determine if the device has been reflashed and therefore
                                        // the SPIFSS settings are obsolete and the flashed settings should be used in preference
    uint32_t    fan_min_runtime;
    int32_t     wifi_auto_on_interval;
    char        home_wifi_ssid[33];
    char        home_wifi_password[65];
#if defined(TARGET_RX)
    uint32_t    uart_baud;
    bool        _unused1:1; // invert_tx
    bool        lock_on_first_connection:1;
    bool        dji_permanently_armed:1;
    bool        is_airport:1;
#endif
#if defined(TARGET_TX) || (defined(UNIT_TEST) && !defined(TARGET_RX))
    uint32_t    tlm_report_interval;
    bool        _unused1:1;
    bool        unlock_higher_power:1;
    bool        is_airport:1;
    uint32_t    uart_baud;              // only use for airport
#endif
} __attribute__((packed)) firmware_options_t;

// Layout is PRODUCTNAME DEVICENAME OPTIONS HARDWARE
constexpr size_t ELRSOPTS_PRODUCTNAME_SIZE = 128;
constexpr size_t ELRSOPTS_DEVICENAME_SIZE = 16;
constexpr size_t ELRSOPTS_OPTIONS_SIZE = 512;
constexpr size_t ELRSOPTS_HARDWARE_SIZE = 2048;

// STM32 firmware slot: a fixed block in the image holding the same four fields that ESP firmware
// finds after its sketch. The web flasher finds it by its magic and patches the regions; the
// firmware reads them at boot. Regions are zero-padded and need not be NUL-terminated.
#define TITAN_SLOT_VERSION 1
typedef struct __attribute__((packed)) {
    uint8_t  magic[8];                                   // "TLRSOPTS"; exactly once in the binary
    uint16_t version;                                    // TITAN_SLOT_VERSION
    uint16_t reserved;                                   // 0
    char     product_name[ELRSOPTS_PRODUCTNAME_SIZE];    // 128, zero-padded
    char     device_name[ELRSOPTS_DEVICENAME_SIZE];      // 16,  zero-padded (lua_name)
    char     options[ELRSOPTS_OPTIONS_SIZE];             // 512, JSON, zero-padded
    char     hardware[ELRSOPTS_HARDWARE_SIZE];           // 2048, JSON, zero-padded
} titan_slot_t;                                          // 2716 bytes

extern char device_name[];
extern firmware_options_t firmwareOptions;
extern bool options_init();

#if !defined(UNIT_TEST) && !defined(PLATFORM_STM32)
extern char product_name[];
extern uint32_t logo_image;
extern String& getOptions();
extern String& getHardware();
extern void saveOptions();
void setOptions(String &options);

#include "EspFlashStream.h"
bool options_HasStringInFlash(EspFlashStream &strmFlash);
void options_SetTrueDefaults();
#elif defined(PLATFORM_STM32)
extern char product_name[];
// Options are persisted in the elrs_eeprom blob — see lib/OPTIONS/options_storage_stm32.h.
// Writers live in lib/ConfigJson (the USB config API); nothing else should call saveOptions().
extern void saveOptions();
extern void options_SetTrueDefaults();
extern bool options_IsCustomised();
extern void options_SetCustomised(bool customised);
// Build-identity value stored with the persisted options and the hardware override; a mismatch
// discards them (see options_storage_stm32.h).
extern uint32_t fw_options_discriminator();
// The effective hardware layout as JSON (empty when there is none).
extern String& getHardware();

extern "C" const volatile titan_slot_t titanSlot;
// The slot's product name (also the USB product string), or "TitanLRS" when none was flashed.
// Reads the slot directly, so it is safe before options_init().
extern "C" const char *titan_ProductName(void);

// Copies a slot region into `dst`, which must hold size + 1 bytes, NUL-terminating it.
static inline void titan_SlotCopy(char *dst, const volatile char *src, const size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        dst[i] = src[i];
    }
    dst[size] = '\0';
}
#endif
