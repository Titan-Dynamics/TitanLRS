#pragma once

#if defined(PLATFORM_STM32)

#include <ArduinoJson.h>

/**
 * @brief Build the document served by `GET /config` on the ESP targets.
 *
 * Mirrors devWIFI.cpp::GetConfiguration(). @p exportMode is the `?export` argument: it emits
 * only the `config` object (with the TX models/vtx/backpack/fan superset) and omits `options`
 * and `settings`, which is what the web UI downloads as models.json / config.json.
 */
void ConfigJson_BuildConfig(JsonObject root, bool exportMode);

/**
 * @brief Build the `options` object (what `GET /options.json` returns on ESP).
 *
 * On STM32 there is no options.json in flash; the object is synthesised from the compile-time
 * `firmwareOptions` so the web UI sees the same shape.
 */
void ConfigJson_BuildOptions(JsonObject options);

/**
 * @brief Apply a posted config document, mirroring `POST /config`.
 *
 * Accepts either the bare config object or a wrapper containing a `config` key (the shape the
 * export produces), matching devWIFI's ImportConfiguration(). Commits to the config store.
 *
 * @return nullptr on success, or a human-readable error string (the text the HTTP handler would
 *         have returned with a 4xx).
 */
const char *ConfigJson_ApplyConfig(JsonVariant json);

/**
 * @brief Apply a posted options document, mirroring `POST /options.json`.
 *
 * Accepts either the bare options object or a wrapper containing an `options` key. Validates the
 * document against this device (`target`, `flash-discriminator`) and each value's range, applies
 * it to `firmwareOptions`, marks the options as customised and persists them.
 *
 * All options are reboot-to-apply; nothing is re-applied live.
 *
 * @return nullptr on success, or a human-readable error string (device left unchanged for the
 *         guard failures; range failures are rejected before anything is persisted).
 */
const char *ConfigJson_ApplyOptions(JsonVariant json);

/**
 * @brief Mirrors the config/model part of devWIFI.cpp::HandleReset().
 *
 * @param resetConfig  reset the config/model store to defaults (`?config` / `?model`)
 * @param resetOptions discard the persisted firmware options and re-seed from the flashed values
 *                     (`?options`); on RX it also performs the modelid/force-tlm reset the ESP
 *                     path does.
 */
void ConfigJson_Reset(bool resetConfig, bool resetOptions);

#endif /* PLATFORM_STM32 */
