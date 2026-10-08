#include "options.h"
#include "hardware.h"
#include "hardware_layout.h"
#include "hardware_override_stm32.h"
#include "elrs_eeprom.h"

#include <string.h>

typedef enum {
    INT,
    BOOL,
    FLOAT,
    ARRAY,
    COUNT
} datatype_t;

static const struct {
    const nameType position;
    const char *name;
    const datatype_t type;
} fields[] = {
    {HARDWARE_customised, "customised", BOOL},
    {HARDWARE_serial_rx, "serial_rx", INT},
    {HARDWARE_serial_tx, "serial_tx", INT},
    {HARDWARE_serial1_rx, "serial1_rx", INT},
    {HARDWARE_serial1_tx, "serial1_tx", INT},
    {HARDWARE_radio_busy, "radio_busy", INT},
    {HARDWARE_radio_busy_2, "radio_busy_2", INT},
    {HARDWARE_radio_dio0, "radio_dio0", INT},
    {HARDWARE_radio_dio0_2, "radio_dio0_2", INT},
    {HARDWARE_radio_dio1, "radio_dio1", INT},
    {HARDWARE_radio_dio1_2, "radio_dio1_2", INT},
    {HARDWARE_radio_miso, "radio_miso", INT},
    {HARDWARE_radio_mosi, "radio_mosi", INT},
    {HARDWARE_radio_nss, "radio_nss", INT},
    {HARDWARE_radio_nss_2, "radio_nss_2", INT},
    {HARDWARE_radio_rst, "radio_rst", INT},
    {HARDWARE_radio_rst_2, "radio_rst_2", INT},
    {HARDWARE_radio_sck, "radio_sck", INT},
    {HARDWARE_radio_dcdc, "radio_dcdc", BOOL},
    {HARDWARE_radio_rfo_hf, "radio_rfo_hf", BOOL},
    {HARDWARE_radio_rfsw_ctrl, "radio_rfsw_ctrl", ARRAY},
    {HARDWARE_radio_rfsw_ctrl_count, "radio_rfsw_ctrl", COUNT},
    {HARDWARE_ant_ctrl, "ant_ctrl", INT},
    {HARDWARE_ant_ctrl_compl, "ant_ctrl_compl", INT},
    {HARDWARE_power_enable, "power_enable", INT},
    {HARDWARE_power_apc2, "power_apc2", INT},
    {HARDWARE_power_rxen, "power_rxen", INT},
    {HARDWARE_power_txen, "power_txen", INT},
    {HARDWARE_power_rxen_2, "power_rxen_2", INT},
    {HARDWARE_power_txen_2, "power_txen_2", INT},
    {HARDWARE_power_lna_gain, "power_lna_gain", INT},
    {HARDWARE_power_min, "power_min", INT},
    {HARDWARE_power_high, "power_high", INT},
    {HARDWARE_power_max, "power_max", INT},
    {HARDWARE_power_default, "power_default", INT},
    {HARDWARE_power_pdet, "power_pdet", INT},
    {HARDWARE_power_pdet_intercept, "power_pdet_intercept", FLOAT},
    {HARDWARE_power_pdet_slope, "power_pdet_slope", FLOAT},
    {HARDWARE_power_control, "power_control", INT},
    {HARDWARE_power_values, "power_values", ARRAY},
    {HARDWARE_power_values_count, "power_values", COUNT},
    {HARDWARE_power_values2, "power_values2", ARRAY},
    {HARDWARE_power_values_dual, "power_values_dual", ARRAY},
    {HARDWARE_power_values_dual_count, "power_values_dual", COUNT},
    {HARDWARE_joystick, "joystick", INT},
    {HARDWARE_joystick_values, "joystick_values", ARRAY},
    {HARDWARE_five_way1, "five_way1", INT},
    {HARDWARE_five_way2, "five_way2", INT},
    {HARDWARE_five_way3, "five_way3", INT},
    {HARDWARE_button, "button", INT},
    {HARDWARE_button_led_index, "button_led_index", INT},
    {HARDWARE_button_active_high, "button_active_high", BOOL},
    {HARDWARE_button2, "button2", INT},
    {HARDWARE_button2_led_index, "button2_led_index", INT},
    {HARDWARE_button2_active_high, "button2_active_high", BOOL},
    {HARDWARE_led, "led", INT},
    {HARDWARE_led_blue, "led_blue", INT},
    {HARDWARE_led_blue_invert, "led_blue_invert", BOOL},
    {HARDWARE_led_green, "led_green", INT},
    {HARDWARE_led_green_invert, "led_green_invert", BOOL},
    {HARDWARE_led_green_red, "led_green_red", INT},
    {HARDWARE_led_red, "led_red", INT},
    {HARDWARE_led_red_invert, "led_red_invert", BOOL},
    {HARDWARE_led_red_green, "led_red_green", INT},
    {HARDWARE_led_rgb, "led_rgb", INT},
    {HARDWARE_led_rgb_isgrb, "led_rgb_isgrb", BOOL},
    {HARDWARE_ledidx_rgb_status, "ledidx_rgb_status", ARRAY},
    {HARDWARE_ledidx_rgb_status_count, "ledidx_rgb_status", COUNT},
    {HARDWARE_ledidx_rgb_vtx, "ledidx_rgb_vtx", ARRAY},
    {HARDWARE_ledidx_rgb_vtx_count, "ledidx_rgb_vtx", COUNT},
    {HARDWARE_ledidx_rgb_boot, "ledidx_rgb_boot", ARRAY},
    {HARDWARE_ledidx_rgb_boot_count, "ledidx_rgb_boot", COUNT},
    {HARDWARE_screen_cs, "screen_cs", INT},
    {HARDWARE_screen_dc, "screen_dc", INT},
    {HARDWARE_screen_mosi, "screen_mosi", INT},
    {HARDWARE_screen_rst, "screen_rst", INT},
    {HARDWARE_screen_sck, "screen_sck", INT},
    {HARDWARE_screen_sda, "screen_sda", INT},
    {HARDWARE_screen_type, "screen_type", INT},
    {HARDWARE_screen_reversed, "screen_reversed", BOOL},
    {HARDWARE_screen_bl, "screen_bl", INT},
    {HARDWARE_use_backpack, "use_backpack", BOOL},
    {HARDWARE_debug_backpack_baud, "debug_backpack_baud", INT},
    {HARDWARE_debug_backpack_rx, "debug_backpack_rx", INT},
    {HARDWARE_debug_backpack_tx, "debug_backpack_tx", INT},
    {HARDWARE_backpack_boot, "backpack_boot", INT},
    {HARDWARE_backpack_en, "backpack_en", INT},
    {HARDWARE_passthrough_baud, "passthrough_baud", INT},
    {HARDWARE_i2c_scl, "i2c_scl", INT},
    {HARDWARE_i2c_sda, "i2c_sda", INT},
    {HARDWARE_misc_gsensor_int, "misc_gsensor_int", INT},
    {HARDWARE_misc_buzzer, "misc_buzzer", INT},
    {HARDWARE_misc_fan_en, "misc_fan_en", INT},
    {HARDWARE_misc_fan_pwm, "misc_fan_pwm", INT},
    {HARDWARE_misc_fan_tacho, "misc_fan_tacho", INT},
    {HARDWARE_misc_fan_speeds, "misc_fan_speeds", ARRAY},
    {HARDWARE_misc_fan_speeds_count, "misc_fan_speeds", COUNT},
    {HARDWARE_gsensor_stk8xxx, "gsensor_stk8xxx", BOOL},
    {HARDWARE_thermal_lm75a, "thermal_lm75a", BOOL},
    {HARDWARE_config_flash_cs, "config_flash_cs", INT},
    {HARDWARE_config_flash_sck, "config_flash_sck", INT},
    {HARDWARE_config_flash_miso, "config_flash_miso", INT},
    {HARDWARE_config_flash_mosi, "config_flash_mosi", INT},
    {HARDWARE_pwm_outputs, "pwm_outputs", ARRAY},
    {HARDWARE_pwm_outputs_count, "pwm_outputs", COUNT},
    {HARDWARE_vbat, "vbat", INT},
    {HARDWARE_vbat_offset, "vbat_offset", INT},
    {HARDWARE_vbat_scale, "vbat_scale", INT},
    {HARDWARE_vbat_atten, "vbat_atten", INT},
    {HARDWARE_vtx_amp_pwm, "vtx_amp_pwm", INT},
    {HARDWARE_vtx_amp_vpd, "vtx_amp_vpd", INT},
    {HARDWARE_vtx_amp_vref, "vtx_amp_vref", INT},
    {HARDWARE_vtx_nss, "vtx_nss", INT},
    {HARDWARE_vtx_miso, "vtx_miso", INT},
    {HARDWARE_vtx_mosi, "vtx_mosi", INT},
    {HARDWARE_vtx_sck, "vtx_sck", INT},
    {HARDWARE_vtx_amp_vpd_25mW, "vtx_amp_vpd_25mW", ARRAY},
    {HARDWARE_vtx_amp_vpd_100mW, "vtx_amp_vpd_100mW", ARRAY},
    {HARDWARE_vtx_amp_pwm_25mW, "vtx_amp_pwm_25mW", ARRAY},
    {HARDWARE_vtx_amp_pwm_100mW, "vtx_amp_pwm_100mW", ARRAY},
};

typedef union {
    int int_value;
    bool bool_value;
    float float_value;
    int16_t *array_value;
} data_holder_t;

static data_holder_t hardware[HARDWARE_LAST];

int hardware_ParsePinName(const char *name)
{
    // "P" + port letter A..K + pin number 0..15, nothing else
    if (name == nullptr || name[0] != 'P' || name[1] < 'A' || name[1] > 'K')
    {
        return -1;
    }
    const int port = name[1] - 'A';
    const char *num = &name[2];
    const size_t digits = strlen(num);
    if (digits == 0 || digits > 2 || num[0] < '0' || num[0] > '9')
    {
        return -1;
    }
    int pin = num[0] - '0';
    if (digits == 2)
    {
        if (num[0] != '1' || num[1] < '0' || num[1] > '5')
        {
            return -1;
        }
        pin = 10 + (num[1] - '0');
    }
    return (port << 4) | pin;
}

// A pin given by name in the layout. STM32 resolves it to the Arduino digital pin; the native
// tests keep the PinName value, and the ESP targets have no pin names.
static int hardware_PinFromName(const char *name)
{
#if defined(PLATFORM_STM32)
    const int pinName = hardware_ParsePinName(name);
    if (pinName < 0)
    {
        return UNDEF_PIN;
    }
    const uint32_t pin = pinNametoDigitalPin((PinName)pinName);
    return pin >= NUM_DIGITAL_PINS ? UNDEF_PIN : (int)pin;
#elif defined(UNIT_TEST)
    return hardware_ParsePinName(name);
#else
    (void)name;
    return -1;
#endif
}

void hardware_ClearAllFields()
{
    for (auto field : fields) {
        switch (field.type) {
            case INT:
                hardware[field.position].int_value = -1;
                break;
            case BOOL:
                hardware[field.position].bool_value = false;
                break;
            case FLOAT:
                hardware[field.position].float_value = 0.0;
                break;
            case ARRAY:
                delete[] hardware[field.position].array_value;
                hardware[field.position].array_value = nullptr;
                break;
            case COUNT:
                hardware[field.position].int_value = 0;
                break;
        }
    }
}

void hardware_LoadFieldsFromDoc(JsonDocument &doc)
{
    for (auto field : fields) {
        if (doc[field.name].is<JsonVariant>()) {
            switch (field.type) {
                case INT:
                    if (doc[field.name].is<const char *>())
                    {
                        hardware[field.position].int_value = hardware_PinFromName(doc[field.name].as<const char *>());
                    }
                    else
                    {
                        hardware[field.position].int_value = doc[field.name];
                    }
                    break;
                case BOOL:
                    hardware[field.position].bool_value = doc[field.name];
                    break;
                case FLOAT:
                    hardware[field.position].float_value = doc[field.name];
                    break;
                case ARRAY:
                    {
                        JsonArray array = doc[field.name].as<JsonArray>();
                        hardware[field.position].array_value = new int16_t[array.size()];
                        copyArray(array, hardware[field.position].array_value, array.size());
                    }
                    break;
                case COUNT:
                    {
                        JsonArray array = doc[field.name].as<JsonArray>();
                        hardware[field.position].int_value = (int)array.size();
                    }
                    break;
            }
        }
    }
}

static const char *const configFlashKeys[] = {
    "config_flash_cs", "config_flash_sck", "config_flash_miso", "config_flash_mosi"
};

JsonDocument hardware_ApplyOverride(JsonDocument &slotDoc, JsonDocument &overrideDoc)
{
    JsonDocument effective = overrideDoc;
    for (const char *key : configFlashKeys)
    {
        if (slotDoc[key].is<JsonVariant>())
        {
            effective[key] = slotDoc[key];
        }
        else
        {
            effective.remove(key);
        }
    }
    effective["customised"] = true;
    return effective;
}

bool hwOverride_IsValid(const hw_override_header_t &header, const uint8_t *payload, const uint32_t discriminator)
{
    static const uint8_t magic[4] = {'T', 'L', 'H', 'W'};
    if (memcmp(header.magic, magic, sizeof(magic)) != 0 || header.version != HW_OVERRIDE_VERSION)
    {
        return false;
    }
    const uint16_t len = header.len;
    if (len == 0 || len > ELRSOPTS_HARDWARE_SIZE || header.discriminator != discriminator)
    {
        return false;
    }
    return elrs_crc32_update(0, payload, len) == header.crc32;
}

int hardware_pin(nameType name)
{
    return hardware[name].int_value;
}

bool hardware_flag(nameType name)
{
    return hardware[name].bool_value;
}

int hardware_int(nameType name)
{
    return hardware[name].int_value;
}

float hardware_float(nameType name)
{
    return hardware[name].float_value;
}

const int16_t* hardware_i16_array(nameType name)
{
    return hardware[name].array_value;
}

const uint16_t* hardware_u16_array(nameType name)
{
    return (uint16_t *)hardware[name].array_value;
}

#if defined(PLATFORM_STM32)
// STM32: the layout comes from the firmware slot (options.h titanSlot), patched in by the web
// flasher, optionally replaced at boot by an override from the config flash (options.cpp).
static JsonDocument slotHardwareDoc;
static String builtinHardwareConfig;

String& getHardware()
{
    return builtinHardwareConfig;
}

JsonDocument &hardware_SlotDoc()
{
    return slotHardwareDoc;
}

void hardware_LoadDoc(JsonDocument &doc)
{
    hardware_ClearAllFields();
    builtinHardwareConfig = "";
    serializeJson(doc, builtinHardwareConfig);
    hardware_LoadFieldsFromDoc(doc);
}

bool hardware_init()
{
    hardware_ClearAllFields();
    builtinHardwareConfig = "";
    slotHardwareDoc.clear();

#if defined(TITAN_UNIFIED_STM32)
    char *json = (char *)malloc(ELRSOPTS_HARDWARE_SIZE + 1);
    if (json == nullptr)
    {
        return false;
    }
    titan_SlotCopy(json, titanSlot.hardware, ELRSOPTS_HARDWARE_SIZE);
    if (json[0] == '\0')
    {
        free(json);
        return false;
    }
    const DeserializationError error = deserializeJson(slotHardwareDoc, (const char *)json);
    free(json);
    if (error || !slotHardwareDoc.is<JsonObject>())
    {
        slotHardwareDoc.clear();
        return false;
    }

    hardware_LoadDoc(slotHardwareDoc);
    return true;
#else
    // Per-board targets take every pin from their header; there is no layout to load.
    return false;
#endif
}

#elif !defined(UNIT_TEST)
#include "helpers.h"
#include "logging.h"
#include <LittleFS.h>

static String builtinHardwareConfig;

String& getHardware()
{
    File file = LittleFS.open("/hardware.json", "r");
    if (!file || file.isDirectory())
    {
        if (file)
        {
            file.close();
        }
        // Try JSON at the end of the firmware
        return builtinHardwareConfig;
    }
    builtinHardwareConfig = file.readString();
    return builtinHardwareConfig;
}

bool hardware_init(EspFlashStream &strmFlash)
{
    hardware_ClearAllFields();
    builtinHardwareConfig.clear();

    Stream *strmSrc;
    JsonDocument doc;
    File file = LittleFS.open("/hardware.json", "r");
    if (!file || file.isDirectory()) {
        constexpr size_t hardwareConfigOffset = ELRSOPTS_PRODUCTNAME_SIZE + ELRSOPTS_DEVICENAME_SIZE + ELRSOPTS_OPTIONS_SIZE;
        strmFlash.setPosition(hardwareConfigOffset);
        if (!options_HasStringInFlash(strmFlash))
        {
            return false;
        }

        strmSrc = &strmFlash;
    }
    else
    {
        strmSrc = &file;
    }

    DeserializationError error = deserializeJson(doc, *strmSrc);
    if (error)
    {
        return false;
    }
    serializeJson(doc, builtinHardwareConfig);

    hardware_LoadFieldsFromDoc(doc);

    return true;
}
#endif
