#pragma once

/*
 * The flashed-options mapping for the unified STM32 targets: the `options` JSON the web flasher
 * patches into the firmware slot uses the ESP options.json keys (options.cpp
 * options_LoadFromFlashOrFile), and is applied on top of the compile-time defaults.
 *
 * It lives in a header so each translation unit sees firmware_options_t for its own target —
 * the native tests build it once as TX and once as RX.
 *
 * Differences from the ESP mapping, which rebuilds every option from the document:
 *   - an absent key keeps the default it is applied over;
 *   - `domain` is range-checked against the domain table (an out-of-range value is ignored,
 *     as FHSS.cpp indexes the table with it directly);
 *   - `uid` must be a 6-entry array, anything else is ignored;
 *   - `wifi-*` and unknown keys are ignored.
 */

#include <ArduinoJson.h>
#include <string.h>

#include "options.h"

static inline void options_ApplyDoc(JsonDocument &doc, firmware_options_t &opts, const uint8_t domainCount)
{
    if (doc["uid"].is<JsonArray>() && doc["uid"].size() == sizeof(opts.uid))
    {
        copyArray(doc["uid"].as<JsonArray>(), opts.uid, sizeof(opts.uid));
        opts.hasUID = true;
    }
    if (doc["domain"].is<unsigned>() && doc["domain"].as<unsigned>() < domainCount)
    {
        opts.domain = (uint8_t)doc["domain"].as<unsigned>();
    }
    if (doc["flash-discriminator"].is<uint32_t>())
    {
        opts.flash_discriminator = doc["flash-discriminator"].as<uint32_t>();
    }
#if defined(TARGET_RX)
    if (doc["rcvr-uart-baud"].is<uint32_t>()) opts.uart_baud = doc["rcvr-uart-baud"].as<uint32_t>();
    if (doc["lock-on-first-connection"].is<bool>()) opts.lock_on_first_connection = doc["lock-on-first-connection"].as<bool>();
#else
    if (doc["tlm-interval"].is<uint32_t>())
    {
        // A zero interval makes checkSendLinkStatsToHandset() queue link-stats every millis()
        // tick, which blocks the OpenTX mixer sync — see options.cpp.
        const uint32_t interval = doc["tlm-interval"].as<uint32_t>();
        opts.tlm_report_interval = interval ? interval : 1U;
    }
    if (doc["fan-runtime"].is<uint32_t>()) opts.fan_min_runtime = doc["fan-runtime"].as<uint32_t>();
    if (doc["unlock-higher-power"].is<bool>()) opts.unlock_higher_power = doc["unlock-higher-power"].as<bool>();
#endif
}

// Parses `json` and applies it to `opts`. False, with `opts` untouched, if it does not parse to an
// object.
static inline bool options_ApplyJson(const char *json, firmware_options_t &opts, const uint8_t domainCount)
{
    JsonDocument doc;
    if (deserializeJson(doc, json) || !doc.is<JsonObject>())
    {
        return false;
    }
    options_ApplyDoc(doc, opts, domainCount);
    return true;
}
