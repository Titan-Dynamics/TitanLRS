#include "ConfigJSON.h"

#ifdef PLATFORM_STM32

#include <stdio.h>
#include "options.h"
#include "FHSS.h"
#include "common.h"

#if defined(TARGET_RX)
#include "config.h"
#endif

int buildConfigJSON(char* buf, size_t maxLen)
{
    int pos = 0;
    #define APPEND(...) pos += snprintf(buf + pos, (pos < (int)maxLen ? maxLen - pos : 0), __VA_ARGS__)

    // ── settings ──────────────────────────────────────────────────────────────
    APPEND("{\"settings\":{");
    APPEND("\"product_name\":\"%s\",", product_name);
    APPEND("\"lua_name\":\"%s\",", device_name);
    APPEND("\"version\":\"%s\",", version);
    APPEND("\"git-commit\":\"%s\",", commit);
    APPEND("\"target\":\"%s\",", (const char*)(target_name + 4)); // skip 4-byte magic
    APPEND("\"custom_hardware\":false,");
#if defined(TARGET_TX)
    APPEND("\"module-type\":\"TX\",");
    // uidtype mirrors the ESP GetConfigUidType() TX branch.
    // "Overridden" would require a runtime customised flag stored in EEPROM — add that
    // once the POST /binding save API is implemented.
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

    // ── options ───────────────────────────────────────────────────────────────
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

    // ── config ────────────────────────────────────────────────────────────────
    APPEND("},\"config\":{");
    APPEND("\"uid\":[%u,%u,%u,%u,%u,%u]",
           (unsigned)UID[0], (unsigned)UID[1], (unsigned)UID[2],
           (unsigned)UID[3], (unsigned)UID[4], (unsigned)UID[5]);
#if defined(TARGET_RX)
    APPEND(",\"serial-protocol\":%u", (unsigned)config.GetSerialProtocol());
    APPEND(",\"modelid\":%u", (unsigned)config.GetModelId());
    APPEND(",\"force-tlm\":%s", config.GetForceTlmOff() ? "true" : "false");
    APPEND(",\"vbind\":%u", (unsigned)config.GetBindStorage());
    APPEND(",\"sbus-failsafe\":%u", (unsigned)config.GetFailsafeMode());
#endif

    APPEND("}}");

    #undef APPEND

    // Ensure null termination even if truncated
    if (pos < (int)maxLen) {
        buf[pos] = '\0';
    } else if (maxLen > 0) {
        buf[maxLen - 1] = '\0';
    }
    return pos;
}

#endif // PLATFORM_STM32
