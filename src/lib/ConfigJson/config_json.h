#pragma once

/*
 * config_json — STM32-only mirror of the config JSON handlers in lib/WIFI/devWIFI.cpp
 * ==================================================================================
 *
 * WHY THIS IS A COPY AND NOT A REFACTOR
 * -------------------------------------
 * TitanLRS is hand-rebased onto new upstream ExpressLRS releases semi-regularly.
 * `lib/WIFI/devWIFI.cpp` is touched often upstream, so extracting the JSON build/apply
 * logic out of it would tax every single rebase. Instead this library is a *copy-adapt*
 * of those handlers with the ESP-only pieces dropped, and `devWIFI.cpp` is left byte-for-byte
 * untouched.
 *
 * MIRROR-MAINTENANCE OBLIGATION (read this during every upstream rebase)
 * ---------------------------------------------------------------------
 * The cost of the above is that this file is a mirror that can drift. When a rebase brings
 * new config fields into devWIFI's handlers, they MUST be hand-mirrored here.
 *
 * Source: lib/WIFI/devWIFI.cpp, functions
 *   - GetConfiguration()        -> ConfigJson_BuildConfig()
 *   - GetConfigUidType()        -> (static) getConfigUidType()
 *   - UpdateConfiguration()     -> ConfigJson_ApplyConfig()   (both the TX and the RX variant)
 *   - UpdateSettings()          -> ConfigJson_ApplyOptions()  (POST /options.json; devWIFI stores
 *                                  the posted document verbatim, STM32 applies it field by field)
 *   - ImportConfiguration()     -> ConfigJson_ApplyConfig()   (TX; handles the superset)
 *   - JsonUidToConfig()         -> (static) jsonUidToConfig()  (RX)
 *   - HandleReset()             -> ConfigJson_Reset()          (config/model part only)
 *   - getOptions() payload      -> ConfigJson_BuildOptions()   (STM32 has no options.json,
 *                                  so the object is synthesised from firmwareOptions)
 * Mirrored as of: feature/stm32 @ 4d52ccaa ("Add stm32 tx target support (#6)").
 *
 * DELIBERATE DIVERGENCES FROM devWIFI (all forced by the STM32 platform)
 * ---------------------------------------------------------------------
 *   - `settings.ssid`      : dropped, no WiFi on STM32.
 *   - `settings.mode`      : reported as "USB" instead of "STA"/"AP".
 *   - serial1 / PWM feature bits that reference ESP GPIO symbols (U0TXD_GPIO_NUM etc.) are
 *     dropped; all STM32 RX targets have GPIO_PIN_PWM_OUTPUTS_COUNT == 0 so the `pwm` array
 *     is empty and the panel self-hides.
 *   - LittleFS removals in HandleReset() do not exist on STM32; `?options` maps onto
 *     options_SetTrueDefaults().
 *   - Options are persisted in the elrs_eeprom blob rather than /options.json, so
 *     ConfigJson_ApplyOptions() validates and applies each field instead of storing the document
 *     (see lib/OPTIONS/options_storage_stm32.h). New writable options added upstream must be
 *     mirrored into it by hand.
 *   - WiFi options are accepted and ignored — no WiFi hardware.
 */

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
