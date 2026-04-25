#include "ConfigJSON.h"

#ifdef PLATFORM_STM32

#include <stdio.h>
#include "options.h"
#include "FHSS.h"
#include "common.h"

#if defined(TARGET_RX)
#include "config.h"
#elif defined(TARGET_TX)
#include "config.h"
#endif

// ---------------------------------------------------------------------------
// JSON string escaping helper (escapes " and \ for JSON safety)
// ---------------------------------------------------------------------------
static void jsonEscapeStr(char* dst, const char* src, size_t dstLen)
{
    size_t out = 0;
    for (size_t i = 0; src[i] && out < dstLen - 1; i++) {
        char c = src[i];
        if ((c == '"' || c == '\\') && out + 2 < dstLen) {
            dst[out++] = '\\';
            dst[out++] = c;
        } else if (c >= 0x20) {
            dst[out++] = c;
        }
        // Skip control characters
    }
    dst[out] = '\0';
}

// ---------------------------------------------------------------------------
// buildOptionsJSON — GET /options.json response
// ---------------------------------------------------------------------------
int buildOptionsJSON(char* buf, size_t maxLen)
{
    int pos = 0;
    #define APPEND(...) pos += snprintf(buf + pos, (pos < (int)maxLen ? maxLen - pos : 0), __VA_ARGS__)

    APPEND("{");

    if (firmwareOptions.hasUID) {
        APPEND("\"uid\":[%u,%u,%u,%u,%u,%u],",
               (unsigned)firmwareOptions.uid[0], (unsigned)firmwareOptions.uid[1],
               (unsigned)firmwareOptions.uid[2], (unsigned)firmwareOptions.uid[3],
               (unsigned)firmwareOptions.uid[4], (unsigned)firmwareOptions.uid[5]);
    }

    // wifi-on-interval: -1 means disabled
    if (firmwareOptions.wifi_auto_on_interval < 0)
        APPEND("\"wifi-on-interval\":-1,");
    else
        APPEND("\"wifi-on-interval\":%ld,", (long)(firmwareOptions.wifi_auto_on_interval / 1000));

    if (firmwareOptions.home_wifi_ssid[0]) {
        char escapedSsid[70], escapedPass[140];
        jsonEscapeStr(escapedSsid, firmwareOptions.home_wifi_ssid, sizeof(escapedSsid));
        jsonEscapeStr(escapedPass, firmwareOptions.home_wifi_password, sizeof(escapedPass));
        APPEND("\"wifi-ssid\":\"%s\",", escapedSsid);
        APPEND("\"wifi-password\":\"%s\",", escapedPass);
    }

#if defined(TARGET_RX)
    APPEND("\"rcvr-uart-baud\":%lu,", (unsigned long)firmwareOptions.uart_baud);
    APPEND("\"lock-on-first-connection\":%s,", firmwareOptions.lock_on_first_connection ? "true" : "false");
#elif defined(TARGET_TX)
    APPEND("\"tlm-interval\":%lu,", (unsigned long)firmwareOptions.tlm_report_interval);
    APPEND("\"fan-runtime\":%lu,", (unsigned long)firmwareOptions.fan_min_runtime);
    APPEND("\"unlock-higher-power\":%s,", firmwareOptions.unlock_higher_power ? "true" : "false");
    APPEND("\"airport-uart-baud\":%lu,", (unsigned long)firmwareOptions.uart_baud);
#endif

    APPEND("\"is-airport\":%s,", firmwareOptions.is_airport ? "true" : "false");
    APPEND("\"domain\":%u,", (unsigned)firmwareOptions.domain);
    // STM32 always reads options from EEPROM, so treat as customised once saved.
    APPEND("\"customised\":true,");
    APPEND("\"flash-discriminator\":%lu", (unsigned long)firmwareOptions.flash_discriminator);
    APPEND("}");

    #undef APPEND

    if (pos < (int)maxLen) buf[pos] = '\0';
    else if (maxLen > 0) buf[maxLen - 1] = '\0';
    return pos;
}

// ---------------------------------------------------------------------------
// buildConfigJSON — GET /config (and GET /config?export) response
// ---------------------------------------------------------------------------
int buildConfigJSON(char* buf, size_t maxLen, bool exportMode)
{
    int pos = 0;
    #define APPEND(...) pos += snprintf(buf + pos, (pos < (int)maxLen ? maxLen - pos : 0), __VA_ARGS__)

    if (!exportMode) {
        // ── settings ──────────────────────────────────────────────────────────
        APPEND("{\"settings\":{");
        APPEND("\"product_name\":\"%s\",", product_name);
        APPEND("\"lua_name\":\"%s\",", device_name);
        APPEND("\"version\":\"%s\",", version);
        APPEND("\"git-commit\":\"%s\",", commit);
        APPEND("\"target\":\"%s\",", (const char*)(target_name + 4)); // skip 4-byte magic
        APPEND("\"custom_hardware\":false,");
#if defined(TARGET_TX)
        APPEND("\"module-type\":\"TX\",");
        if (firmwareOptions.hasUID)
            APPEND("\"uidtype\":\"Flashed\",");
        else
            APPEND("\"uidtype\":\"Not set (using MAC address)\",");
#else
        APPEND("\"module-type\":\"RX\",");
#endif

#if defined(RADIO_SX128X)
        APPEND("\"radio-type\":\"SX128X\",");
        APPEND("\"has_low_band\":false,");
        APPEND("\"has_high_band\":true,");
        if (FHSSconfig && FHSSconfig->domain)
            APPEND("\"reg_domain_high\":\"%s\"", FHSSconfig->domain);
        else
            APPEND("\"reg_domain_high\":null");
#elif defined(RADIO_SX127X)
        APPEND("\"radio-type\":\"SX127X\",");
        APPEND("\"has_low_band\":true,");
        APPEND("\"has_high_band\":false,");
        if (FHSSconfig && FHSSconfig->domain)
            APPEND("\"reg_domain_low\":\"%s\"", FHSSconfig->domain);
        else
            APPEND("\"reg_domain_low\":null");
#elif defined(RADIO_LR1121)
        APPEND("\"radio-type\":\"LR1121\",");
        APPEND("\"has_low_band\":true,");
        APPEND("\"has_high_band\":true,");
        if (FHSSconfig && FHSSconfig->domain)
            APPEND("\"reg_domain_low\":\"%s\",", FHSSconfig->domain);
        else
            APPEND("\"reg_domain_low\":null,");
        if (FHSSconfigDualBand && FHSSconfigDualBand->domain)
            APPEND("\"reg_domain_high\":\"%s\"", FHSSconfigDualBand->domain);
        else
            APPEND("\"reg_domain_high\":null");
#else
        APPEND("\"radio-type\":\"unknown\"");
#endif

        // ── options ───────────────────────────────────────────────────────────
        APPEND("},\"options\":{");
        APPEND("\"wifi-on-interval\":%ld,", (long)(firmwareOptions.wifi_auto_on_interval / 1000));
        APPEND("\"is-airport\":%s,", firmwareOptions.is_airport ? "true" : "false");
        APPEND("\"domain\":%u,", (unsigned)firmwareOptions.domain);
#if defined(TARGET_RX)
        APPEND("\"rcvr-uart-baud\":%lu,", (unsigned long)firmwareOptions.uart_baud);
        APPEND("\"lock-on-first-connection\":%s", firmwareOptions.lock_on_first_connection ? "true" : "false");
#elif defined(TARGET_TX)
        APPEND("\"airport-uart-baud\":%lu,", (unsigned long)firmwareOptions.uart_baud);
        APPEND("\"tlm-interval\":%lu", (unsigned long)firmwareOptions.tlm_report_interval);
#else
        APPEND("\"rcvr-uart-baud\":420000,");
        APPEND("\"lock-on-first-connection\":true");
#endif

        // ── config ────────────────────────────────────────────────────────────
        APPEND("},\"config\":{");
    } else {
        // Export mode: only the config section
        APPEND("{\"config\":{");
    }

    // UID is always included
    APPEND("\"uid\":[%u,%u,%u,%u,%u,%u]",
           (unsigned)UID[0], (unsigned)UID[1], (unsigned)UID[2],
           (unsigned)UID[3], (unsigned)UID[4], (unsigned)UID[5]);

#if defined(TARGET_RX)
    APPEND(",\"serial-protocol\":%u", (unsigned)config.GetSerialProtocol());
    APPEND(",\"modelid\":%u", (unsigned)config.GetModelId());
    APPEND(",\"force-tlm\":%s", config.GetForceTlmOff() ? "true" : "false");
    APPEND(",\"vbind\":%u", (unsigned)config.GetBindStorage());
    APPEND(",\"sbus-failsafe\":%u", (unsigned)config.GetFailsafeMode());

#elif defined(TARGET_TX)
    // TX button-actions
    APPEND(",\"button-actions\":[");
    for (int btn = 0; btn < 2; btn++) {
        const tx_button_color_t* bc = config.GetButtonActions(btn);
        APPEND("%s{\"color\":%u,\"action\":[", btn > 0 ? "," : "", (unsigned)bc->val.color);
        for (int act = 0; act < CONFIG_TX_BUTTON_ACTION_CNT; act++) {
            APPEND("%s{\"is-long-press\":%s,\"count\":%u,\"action\":%u}",
                   act > 0 ? "," : "",
                   bc->val.actions[act].pressType ? "true" : "false",
                   (unsigned)bc->val.actions[act].count,
                   (unsigned)bc->val.actions[act].action);
        }
        APPEND("]}");
    }
    APPEND("]");

    if (exportMode) {
        // Extended export fields — fan, motion, vtx, backpack, all model configs
        APPEND(",\"fan-mode\":%u", (unsigned)config.GetFanMode());
        APPEND(",\"power-fan-threshold\":%u", (unsigned)config.GetPowerFanThreshold());
        APPEND(",\"motion-mode\":%u", (unsigned)config.GetMotionMode());
        APPEND(",\"vtx-admin\":{\"band\":%u,\"channel\":%u,\"pitmode\":%u,\"power\":%u}",
               (unsigned)config.GetVtxBand(), (unsigned)config.GetVtxChannel(),
               (unsigned)config.GetVtxPitmode(), (unsigned)config.GetVtxPower());
        APPEND(",\"backpack\":{\"disabled\":%s,\"dvr-start-delay\":%u,\"dvr-stop-delay\":%u,\"dvr-aux-channel\":%u,\"telemetry-mode\":%u}",
               config.GetBackpackDisable() ? "true" : "false",
               (unsigned)config.GetDvrStartDelay(), (unsigned)config.GetDvrStopDelay(),
               (unsigned)config.GetDvrAux(), (unsigned)config.GetBackpackTlmMode());
        APPEND(",\"model\":{");
        for (int model = 0; model < CONFIG_TX_MODEL_CNT; model++) {
            const model_config_t& mc = config.GetModelConfig(model);
            APPEND("%s\"%d\":{\"packet-rate\":%u,\"telemetry-ratio\":%u,\"switch-mode\":%u,"
                   "\"link-mode\":%u,\"model-match\":%s,\"tx-antenna\":%u,"
                   "\"ptr-start-chan\":%u,\"ptr-enable-chan\":%u,"
                   "\"power\":{\"max-power\":%u,\"dynamic-power\":%s,\"boost-channel\":%u}}",
                   model > 0 ? "," : "",
                   model,
                   (unsigned)mc.rate, (unsigned)mc.tlm, (unsigned)mc.switchMode,
                   (unsigned)mc.linkMode, mc.modelMatch ? "true" : "false",
                   (unsigned)mc.txAntenna,
                   (unsigned)mc.ptrStartChannel, (unsigned)mc.ptrEnableChannel,
                   (unsigned)mc.power, mc.dynamicPower ? "true" : "false",
                   (unsigned)mc.boostChannel);
        }
        APPEND("}");
    }
#endif

    APPEND("}}");

    #undef APPEND

    // Ensure null termination even if truncated
    if (pos < (int)maxLen) buf[pos] = '\0';
    else if (maxLen > 0) buf[maxLen - 1] = '\0';
    return pos;
}

#endif // PLATFORM_STM32
