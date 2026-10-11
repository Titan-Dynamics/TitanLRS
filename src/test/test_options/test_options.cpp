#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unity.h>

#include "options.h"
#include "options_apply.h"
#include "hardware.h"
#include "hardware_layout.h"
#include "hardware_override_stm32.h"
#include "elrs_eeprom.h"

// test_options_rx.cpp — the same mapping built for an RX
void test_apply_doc_rx_keys();
void test_apply_doc_rx_ignores_tx_keys();

static_assert(sizeof(titan_slot_t) == 2716, "titan_slot_t layout is shared with the web flasher");

static std::string readFixture(const char *name)
{
    std::string path(__FILE__);
    path = path.substr(0, path.find_last_of('/') + 1) + name;
    FILE *f = fopen(path.c_str(), "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path.c_str());
    std::string out;
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        out.append(buf, n);
    }
    fclose(f);
    return out;
}

static void loadLayout(const char *name)
{
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, readFixture(name)));
    hardware_ClearAllFields();
    hardware_LoadFieldsFromDoc(doc);
}

// ---- slot ------------------------------------------------------------------------------

void test_slot_layout()
{
    TEST_ASSERT_EQUAL(2716, sizeof(titan_slot_t));
    TEST_ASSERT_EQUAL(12, offsetof(titan_slot_t, product_name));
    TEST_ASSERT_EQUAL(12 + 128, offsetof(titan_slot_t, device_name));
    TEST_ASSERT_EQUAL(12 + 128 + 16, offsetof(titan_slot_t, options));
    TEST_ASSERT_EQUAL(12 + 128 + 16 + 512, offsetof(titan_slot_t, hardware));
}

// ---- pin names -------------------------------------------------------------------------

void test_parse_pin_name()
{
    TEST_ASSERT_EQUAL(0, hardware_ParsePinName("PA0"));
    TEST_ASSERT_EQUAL(76, hardware_ParsePinName("PE12"));
    TEST_ASSERT_EQUAL(175, hardware_ParsePinName("PK15"));
    TEST_ASSERT_EQUAL(0x1A, hardware_ParsePinName("PB10"));

    TEST_ASSERT_EQUAL(-1, hardware_ParsePinName("PZ1"));
    TEST_ASSERT_EQUAL(-1, hardware_ParsePinName("PE16"));
    TEST_ASSERT_EQUAL(-1, hardware_ParsePinName(""));
    TEST_ASSERT_EQUAL(-1, hardware_ParsePinName("PE"));
    TEST_ASSERT_EQUAL(-1, hardware_ParsePinName("PE01"));
    TEST_ASSERT_EQUAL(-1, hardware_ParsePinName("PE123"));
    TEST_ASSERT_EQUAL(-1, hardware_ParsePinName("pe1"));
    TEST_ASSERT_EQUAL(-1, hardware_ParsePinName("PE1x"));
    TEST_ASSERT_EQUAL(-1, hardware_ParsePinName("12"));
    TEST_ASSERT_EQUAL(-1, hardware_ParsePinName(nullptr));
}

// ---- layout fixtures (Part C) ----------------------------------------------------------

void test_layout_tx()
{
    loadLayout("TD LR2021 STM32H7 Gemini TX.json");
    TEST_ASSERT_EQUAL(hardware_ParsePinName("PE0"), hardware_pin(HARDWARE_radio_nss));
    TEST_ASSERT_EQUAL(hardware_ParsePinName("PE8"), hardware_pin(HARDWARE_radio_nss_2));
    TEST_ASSERT_EQUAL(hardware_ParsePinName("PB10"), hardware_pin(HARDWARE_serial_rx));
    TEST_ASSERT_EQUAL(hardware_ParsePinName("PB10"), hardware_pin(HARDWARE_serial_tx));
    TEST_ASSERT_EQUAL(hardware_ParsePinName("PD6"), hardware_pin(HARDWARE_config_flash_cs));
    TEST_ASSERT_EQUAL(hardware_ParsePinName("PD7"), hardware_pin(HARDWARE_config_flash_mosi));
    TEST_ASSERT_TRUE(hardware_flag(HARDWARE_button_active_high));
    TEST_ASSERT_FALSE(hardware_flag(HARDWARE_button2_active_high));
    TEST_ASSERT_TRUE(hardware_flag(HARDWARE_radio_dcdc));
    TEST_ASSERT_EQUAL(4, hardware_int(HARDWARE_power_values_count));
    TEST_ASSERT_EQUAL(4, hardware_int(HARDWARE_power_values_dual_count));
    TEST_ASSERT_EQUAL(37, hardware_i16_array(HARDWARE_power_values)[3]);
    TEST_ASSERT_EQUAL(24, hardware_i16_array(HARDWARE_power_values_dual)[3]);
    TEST_ASSERT_EQUAL(3, hardware_int(HARDWARE_power_max));
    // keys absent from the layout
    TEST_ASSERT_EQUAL(-1, hardware_pin(HARDWARE_button2));
    TEST_ASSERT_EQUAL(-1, hardware_pin(HARDWARE_led_blue));
    TEST_ASSERT_NULL(hardware_i16_array(HARDWARE_pwm_outputs));
}

void test_layout_rx()
{
    loadLayout("TD LR2021 STM32H7 Gemini RX.json");
    TEST_ASSERT_EQUAL(hardware_ParsePinName("PE0"), hardware_pin(HARDWARE_radio_nss));
    TEST_ASSERT_EQUAL(hardware_ParsePinName("PE8"), hardware_pin(HARDWARE_radio_nss_2));
    TEST_ASSERT_EQUAL(hardware_ParsePinName("PB11"), hardware_pin(HARDWARE_serial_rx));
    TEST_ASSERT_EQUAL(hardware_ParsePinName("PB10"), hardware_pin(HARDWARE_serial_tx));
    TEST_ASSERT_EQUAL(hardware_ParsePinName("PD6"), hardware_pin(HARDWARE_config_flash_cs));
    TEST_ASSERT_TRUE(hardware_flag(HARDWARE_button_active_high));
    TEST_ASSERT_EQUAL(4, hardware_int(HARDWARE_power_values_count));
}

void test_layout_integer_and_bad_pins()
{
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, R"({"radio_nss": 5, "radio_sck": "PZ9", "radio_miso": "PE13"})"));
    hardware_ClearAllFields();
    hardware_LoadFieldsFromDoc(doc);
    TEST_ASSERT_EQUAL(5, hardware_pin(HARDWARE_radio_nss));
    TEST_ASSERT_EQUAL(-1, hardware_pin(HARDWARE_radio_sck));
    TEST_ASSERT_EQUAL(77, hardware_pin(HARDWARE_radio_miso));
}

// ---- options_ApplyDoc (TX build) -------------------------------------------------------

static firmware_options_t txDefaults()
{
    firmware_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.domain = 1;
    opts.tlm_report_interval = 240;
    opts.flash_discriminator = 0;
    return opts;
}

void test_apply_doc_tx_keys()
{
    firmware_options_t opts = txDefaults();
    TEST_ASSERT_TRUE(options_ApplyJson(
        R"({"uid":[1,2,3,4,5,6],"domain":3,"flash-discriminator":3735928559,"tlm-interval":500,)"
        R"("fan-runtime":12,"unlock-higher-power":true,"wifi-ssid":"x","wifi-on-interval":60,"other":1})",
        opts, 8));
    const uint8_t uid[6] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_TRUE(opts.hasUID);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(uid, opts.uid, 6);
    TEST_ASSERT_EQUAL(3, opts.domain);
    TEST_ASSERT_EQUAL_UINT32(3735928559U, opts.flash_discriminator);
    TEST_ASSERT_EQUAL(500, opts.tlm_report_interval);
    TEST_ASSERT_EQUAL(12, opts.fan_min_runtime);
    TEST_ASSERT_TRUE(opts.unlock_higher_power);
    // wifi-* ignored
    TEST_ASSERT_EQUAL_STRING("", opts.home_wifi_ssid);
    TEST_ASSERT_EQUAL(0, opts.wifi_auto_on_interval);
}

void test_apply_doc_absent_keys_keep_defaults()
{
    firmware_options_t opts = txDefaults();
    TEST_ASSERT_TRUE(options_ApplyJson("{}", opts, 8));
    TEST_ASSERT_FALSE(opts.hasUID);
    TEST_ASSERT_EQUAL(1, opts.domain);
    TEST_ASSERT_EQUAL(240, opts.tlm_report_interval);
    TEST_ASSERT_EQUAL(0, opts.flash_discriminator);
}

void test_apply_doc_bad_values()
{
    firmware_options_t opts = txDefaults();
    TEST_ASSERT_TRUE(options_ApplyJson(R"({"domain":8,"uid":[1,2,3],"tlm-interval":0})", opts, 8));
    TEST_ASSERT_EQUAL(1, opts.domain);           // out of range: ignored
    TEST_ASSERT_FALSE(opts.hasUID);              // wrong length: ignored
    TEST_ASSERT_EQUAL(1, opts.tlm_report_interval);  // 0 would flood the handset

    TEST_ASSERT_TRUE(options_ApplyJson(R"({"domain":-1})", opts, 8));
    TEST_ASSERT_EQUAL(1, opts.domain);
    TEST_ASSERT_TRUE(options_ApplyJson(R"({"domain":"2"})", opts, 8));
    TEST_ASSERT_EQUAL(1, opts.domain);
    TEST_ASSERT_TRUE(options_ApplyJson(R"({"domain":7})", opts, 8));
    TEST_ASSERT_EQUAL(7, opts.domain);
}

void test_apply_doc_malformed()
{
    firmware_options_t opts = txDefaults();
    const firmware_options_t before = opts;
    TEST_ASSERT_FALSE(options_ApplyJson(R"({"domain":3,)", opts, 8));
    TEST_ASSERT_FALSE(options_ApplyJson("[1,2,3]", opts, 8));
    TEST_ASSERT_FALSE(options_ApplyJson("", opts, 8));
    TEST_ASSERT_EQUAL_MEMORY(&before, &opts, sizeof(opts));
}

// ---- hardware override record ----------------------------------------------------------

static hw_override_header_t makeHeader(const char *json, const uint32_t discriminator)
{
    hw_override_header_t h;
    memcpy(h.magic, "TLHW", 4);
    h.version = HW_OVERRIDE_VERSION;
    h.len = (uint16_t)strlen(json);
    h.discriminator = discriminator;
    h.crc32 = elrs_crc32_update(0, (const uint8_t *)json, strlen(json));
    return h;
}

void test_override_valid()
{
    const char *json = R"({"radio_nss":"PE4"})";
    const hw_override_header_t h = makeHeader(json, 0x12345678);
    TEST_ASSERT_TRUE(hwOverride_IsValid(h, (const uint8_t *)json, 0x12345678));
    // CRC-32/ISO-HDLC check value
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, elrs_crc32_update(0, (const uint8_t *)"123456789", 9));
}

void test_override_invalid()
{
    const char *json = R"({"radio_nss":"PE4"})";
    const uint32_t disc = 0x12345678;

    hw_override_header_t h = makeHeader(json, disc);
    h.magic[3] = 'X';
    TEST_ASSERT_FALSE(hwOverride_IsValid(h, (const uint8_t *)json, disc));

    h = makeHeader(json, disc);
    h.version = 2;
    TEST_ASSERT_FALSE(hwOverride_IsValid(h, (const uint8_t *)json, disc));

    h = makeHeader(json, disc);
    h.crc32 ^= 1;
    TEST_ASSERT_FALSE(hwOverride_IsValid(h, (const uint8_t *)json, disc));

    h = makeHeader(json, disc);
    h.len = 0;
    TEST_ASSERT_FALSE(hwOverride_IsValid(h, (const uint8_t *)json, disc));
    h.len = ELRSOPTS_HARDWARE_SIZE + 1;
    TEST_ASSERT_FALSE(hwOverride_IsValid(h, (const uint8_t *)json, disc));
    h.len = 0xFFFF;   // erased flash
    TEST_ASSERT_FALSE(hwOverride_IsValid(h, (const uint8_t *)json, disc));

    h = makeHeader(json, disc);
    TEST_ASSERT_FALSE(hwOverride_IsValid(h, (const uint8_t *)json, disc + 1));
}

// ---- hardware_ApplyOverride ------------------------------------------------------------

void test_apply_override()
{
    JsonDocument slot;
    TEST_ASSERT_FALSE(deserializeJson(slot, readFixture("TD LR2021 STM32H7 Gemini TX.json")));
    JsonDocument over;
    TEST_ASSERT_FALSE(deserializeJson(over, R"({"radio_nss":"PE4","radio_sck":"PE12","config_flash_cs":"PA4","config_flash_sck":"PA5"})"));

    JsonDocument eff = hardware_ApplyOverride(slot, over);
    TEST_ASSERT_EQUAL_STRING("PE4", eff["radio_nss"].as<const char *>());
    // a key absent from the override is absent from the result
    TEST_ASSERT_FALSE(eff["radio_nss_2"].is<JsonVariant>());
    TEST_ASSERT_FALSE(eff["power_values"].is<JsonVariant>());
    // config_flash_* always come from the slot
    TEST_ASSERT_EQUAL_STRING("PD6", eff["config_flash_cs"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("PB3", eff["config_flash_sck"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("PB4", eff["config_flash_miso"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING("PD7", eff["config_flash_mosi"].as<const char *>());
    TEST_ASSERT_TRUE(eff["customised"].as<bool>());

    hardware_ClearAllFields();
    hardware_LoadFieldsFromDoc(eff);
    TEST_ASSERT_EQUAL(hardware_ParsePinName("PE4"), hardware_pin(HARDWARE_radio_nss));
    TEST_ASSERT_EQUAL(-1, hardware_pin(HARDWARE_radio_nss_2));
    TEST_ASSERT_EQUAL(hardware_ParsePinName("PD6"), hardware_pin(HARDWARE_config_flash_cs));
    TEST_ASSERT_TRUE(hardware_flag(HARDWARE_customised));
}

void test_apply_override_slot_without_flash()
{
    JsonDocument slot;
    TEST_ASSERT_FALSE(deserializeJson(slot, R"({"radio_nss":"PE0"})"));
    JsonDocument over;
    TEST_ASSERT_FALSE(deserializeJson(over, R"({"radio_nss":"PE4","config_flash_cs":"PA4"})"));
    JsonDocument eff = hardware_ApplyOverride(slot, over);
    TEST_ASSERT_FALSE(eff["config_flash_cs"].is<JsonVariant>());
    TEST_ASSERT_EQUAL_STRING("PE4", eff["radio_nss"].as<const char *>());
    TEST_ASSERT_TRUE(eff["customised"].as<bool>());
}

void setUp() {}
void tearDown() {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_slot_layout);
    RUN_TEST(test_parse_pin_name);
    RUN_TEST(test_layout_tx);
    RUN_TEST(test_layout_rx);
    RUN_TEST(test_layout_integer_and_bad_pins);
    RUN_TEST(test_apply_doc_tx_keys);
    RUN_TEST(test_apply_doc_absent_keys_keep_defaults);
    RUN_TEST(test_apply_doc_bad_values);
    RUN_TEST(test_apply_doc_malformed);
    RUN_TEST(test_apply_doc_rx_keys);
    RUN_TEST(test_apply_doc_rx_ignores_tx_keys);
    RUN_TEST(test_override_valid);
    RUN_TEST(test_override_invalid);
    RUN_TEST(test_apply_override);
    RUN_TEST(test_apply_override_slot_without_flash);
    UNITY_END();

    return 0;
}
