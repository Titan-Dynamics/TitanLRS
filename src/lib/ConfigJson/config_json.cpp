#include "config_json.h"

#if defined(PLATFORM_STM32)

#include "common.h"
#include "config.h"
#include "options.h"
#include "hardware.h"
#include "FHSS.h"
#include "devButton.h"

#include "options_storage_stm32.h"

#if defined(TARGET_TX)
extern TxConfig config;
static_assert(sizeof(tx_config_t) <= FW_OPTIONS_EEPROM_OFFSET,
              "tx_config_t has grown into the persisted firmware options region "
              "(see lib/OPTIONS/options_storage_stm32.h)");
#else
extern RxConfig config;
static_assert(sizeof(rx_config_t) <= FW_OPTIONS_EEPROM_OFFSET,
              "rx_config_t has grown into the persisted firmware options region "
              "(see lib/OPTIONS/options_storage_stm32.h)");
#endif

// ---------------------------------------------------------------------------
// options — devWIFI serves the raw options.json from LittleFS; STM32 has no such
// file, so the same shape is synthesised from the compile-time firmwareOptions.
// Key names must match what html/src/pages/{tx,rx}-options-panel.js reads.
// ---------------------------------------------------------------------------
void ConfigJson_BuildOptions(JsonObject options)
{
    if (firmwareOptions.hasUID)
    {
        const auto uid = options["uid"].to<JsonArray>();
        copyArray(firmwareOptions.uid, sizeof(firmwareOptions.uid), uid);
    }

    options["wifi-on-interval"] = firmwareOptions.wifi_auto_on_interval < 0
                                      ? -1
                                      : firmwareOptions.wifi_auto_on_interval / 1000;
    if (firmwareOptions.home_wifi_ssid[0])
    {
        options["wifi-ssid"] = firmwareOptions.home_wifi_ssid;
        options["wifi-password"] = firmwareOptions.home_wifi_password;
    }

#if defined(TARGET_RX)
    options["rcvr-uart-baud"] = firmwareOptions.uart_baud;
    options["lock-on-first-connection"] = firmwareOptions.lock_on_first_connection;
#else
    options["tlm-interval"] = firmwareOptions.tlm_report_interval;
    options["fan-runtime"] = firmwareOptions.fan_min_runtime;
    options["unlock-higher-power"] = firmwareOptions.unlock_higher_power;
    options["airport-uart-baud"] = firmwareOptions.uart_baud;
#endif

    options["is-airport"] = firmwareOptions.is_airport;
    options["domain"] = firmwareOptions.domain;
    // True once anything has been written over the USB config API and persisted; the flag is
    // stored in the options blob header (lib/OPTIONS/options_storage_stm32.h).
    options["customised"] = options_IsCustomised();
    options["flash-discriminator"] = firmwareOptions.flash_discriminator;
}

// ---------------------------------------------------------------------------
// Copy-adapt of devWIFI.cpp::UpdateSettings() (the `POST /options.json` handler).
//
// devWIFI simply writes the posted document to LittleFS and lets the next boot parse it; STM32
// has no filesystem, so each key is applied to `firmwareOptions` here and the struct is persisted
// by saveOptions(). That means this function — not a boot-time parser — is where validation has
// to happen, hence the range checks that have no devWIFI counterpart.
//
// Every option is reboot-to-apply: UID, domain and the airport baud are all consumed during
// setup(). The host prompts for the reboot (saveWithReboot()).
// ---------------------------------------------------------------------------
const char *ConfigJson_ApplyOptions(JsonVariant json)
{
    if (json["options"].is<JsonVariant>())
    {
        json = json["options"];
    }

    // Refuse a document that was fetched from a different board or a different firmware build.
    // devWIFI only checks the discriminator; `target` is checked too because the USB transport
    // makes it trivial to point a saved file at the wrong device.
    if (json["target"].is<const char *>() &&
        strcmp(json["target"].as<const char *>(), (const char *)&target_name[4]) != 0)
    {
        return "target mismatch";
    }
    if (json["flash-discriminator"].is<JsonVariant>() &&
        json["flash-discriminator"].as<uint32_t>() != firmwareOptions.flash_discriminator)
    {
        return "mismatched device identifier, reload the configuration and try again";
    }

    // Everything below is applied to a scratch copy so that a value rejected half way through
    // leaves the live options — and therefore the running radio — untouched.
    firmware_options_t opts = firmwareOptions;

    // Regulatory domain indexes domains[] in FHSS.cpp directly (FHSS.cpp: FHSSconfig =
    // &domains[firmwareOptions.domain]), so an out-of-range value would read off the end of the
    // table and put the radio on a garbage frequency. On 2.4 GHz-only builds the table has a
    // single entry and any non-zero write is refused.
    if (json["domain"].is<JsonVariant>())
    {
        const uint32_t domain = json["domain"].as<uint32_t>();
        if (domain >= FHSSdomainCount)
        {
            return "unsupported regulatory domain";
        }
        opts.domain = (uint8_t)domain;
    }

    if (json["is-airport"].is<JsonVariant>()) opts.is_airport = json["is-airport"].as<bool>();

#if defined(TARGET_TX)
    // The binding phrase. An empty array clears the override the way a missing `uid` key does on
    // the ESP path (options.cpp::options_LoadFromFlashOrFile). An *absent* key leaves the stored
    // UID alone, unlike ESP: a partial document must not silently unbind the module, and the web
    // UI always posts the whole options object anyway.
    if (json["uid"].is<JsonArray>())
    {
        const auto juid = json["uid"].as<JsonArray>();
        if (juid.size() == 0)
        {
            memset(opts.uid, 0, sizeof(opts.uid));
            opts.hasUID = false;
        }
        else if (juid.size() != sizeof(opts.uid))
        {
            return "uid must be 6 bytes";
        }
        else
        {
            copyArray(juid, opts.uid, sizeof(opts.uid));
            opts.hasUID = true;
        }
    }

    if (json["tlm-interval"].is<JsonVariant>())
    {
        // A zero interval makes checkSendLinkStatsToHandset() queue link-stats every millis()
        // tick, which blocks the OpenTX mixer sync — see options.cpp.
        const uint32_t interval = json["tlm-interval"].as<uint32_t>();
        opts.tlm_report_interval = interval ? interval : 1U;
    }
    if (json["fan-runtime"].is<JsonVariant>()) opts.fan_min_runtime = json["fan-runtime"].as<uint32_t>();
    if (json["unlock-higher-power"].is<JsonVariant>()) opts.unlock_higher_power = json["unlock-higher-power"].as<bool>();
    if (json["airport-uart-baud"].is<JsonVariant>()) opts.uart_baud = json["airport-uart-baud"].as<uint32_t>();
#else
    // RX binding lives in config.uid (jsonUidToConfig), not in firmwareOptions, so a posted `uid`
    // is deliberately ignored here — the flashed UID stays the bind baseline.
    if (json["rcvr-uart-baud"].is<JsonVariant>()) opts.uart_baud = json["rcvr-uart-baud"].as<uint32_t>();
    if (json["lock-on-first-connection"].is<JsonVariant>()) opts.lock_on_first_connection = json["lock-on-first-connection"].as<bool>();
    if (json["dji-permanently-armed"].is<JsonVariant>()) opts.dji_permanently_armed = json["dji-permanently-armed"].as<bool>();
#endif

    // `wifi-ssid` / `wifi-password` / `wifi-on-interval` are accepted and ignored — no WiFi
    // hardware on STM32, but the host posts the whole options object back.

    firmwareOptions = opts;
    options_SetCustomised(true);
    saveOptions();
    return nullptr;
}

// ---------------------------------------------------------------------------
// Mirror of devWIFI.cpp::GetConfigUidType()
// ---------------------------------------------------------------------------
static const char *getConfigUidType(const JsonObject json)
{
#if defined(TARGET_RX)
    if (config.GetBindStorage() == BINDSTORAGE_VOLATILE)
        return "Volatile";
    if (config.GetBindStorage() == BINDSTORAGE_RETURNABLE && config.IsOnLoan())
        return "Loaned";
    if (config.GetIsBound())
        return "Bound";
    return "Not Bound";
#else
    if (firmwareOptions.hasUID)
    {
        if (json["options"]["customised"] | false)
            return "Overridden";
        return "Flashed";
    }
    return "Not set (using MAC address)";
#endif
}

// ---------------------------------------------------------------------------
// Mirror of devWIFI.cpp::GetConfiguration()
// ---------------------------------------------------------------------------
void ConfigJson_BuildConfig(JsonObject json, const bool exportMode)
{
    if (!exportMode)
    {
        ConfigJson_BuildOptions(json["options"].to<JsonObject>());
    }

    const auto cfg = json["config"].to<JsonObject>();
    const auto uid = cfg["uid"].to<JsonArray>();
    copyArray(UID, UID_LEN, uid);

#if defined(TARGET_TX)
    int button_count = 0;
    if (GPIO_PIN_BUTTON != UNDEF_PIN)
        button_count = 1;
    if (GPIO_PIN_BUTTON2 != UNDEF_PIN)
        button_count = 2;
    for (int button = 0; button < button_count; button++)
    {
        const tx_button_color_t *buttonColor = config.GetButtonActions(button);
        const auto btn = cfg["button-actions"][button].to<JsonObject>();
        if (hardware_int(button == 0 ? HARDWARE_button_led_index : HARDWARE_button2_led_index) != -1)
        {
            btn["color"] = buttonColor->val.color;
        }
        for (int pos = 0; pos < (int)button_GetActionCnt(); pos++)
        {
            const auto action = btn["action"][pos].to<JsonObject>();
            action["is-long-press"] = buttonColor->val.actions[pos].pressType ? true : false;
            action["count"] = buttonColor->val.actions[pos].count;
            action["action"] = buttonColor->val.actions[pos].action;
        }
    }
    if (exportMode)
    {
        cfg["fan-mode"] = config.GetFanMode();
        cfg["power-fan-threshold"] = config.GetPowerFanThreshold();
        cfg["motion-mode"] = config.GetMotionMode();

        const auto vtxAdmin = cfg["vtx-admin"].to<JsonObject>();
        vtxAdmin["band"] = config.GetVtxBand();
        vtxAdmin["channel"] = config.GetVtxChannel();
        vtxAdmin["pitmode"] = config.GetVtxPitmode();
        vtxAdmin["power"] = config.GetVtxPower();

        const auto backpack = cfg["backpack"].to<JsonObject>();
        backpack["disabled"] = config.GetBackpackDisable();
        backpack["dvr-start-delay"] = config.GetDvrStartDelay();
        backpack["dvr-stop-delay"] = config.GetDvrStopDelay();
        backpack["dvr-aux-channel"] = config.GetDvrAux();
        backpack["telemetry-mode"] = config.GetBackpackTlmMode();

        for (int model = 0; model < CONFIG_TX_MODEL_CNT; model++)
        {
            const model_config_t &modelConfig = config.GetModelConfig(model);
            char strModel[8];
            snprintf(strModel, sizeof(strModel), "%d", model);
            const auto modelJson = cfg["model"][strModel].to<JsonObject>();
            modelJson["packet-rate"] = modelConfig.rate;
            modelJson["telemetry-ratio"] = modelConfig.tlm;
            modelJson["switch-mode"] = modelConfig.switchMode;
            modelJson["link-mode"] = modelConfig.linkMode;
            modelJson["model-match"] = modelConfig.modelMatch;
            modelJson["tx-antenna"] = modelConfig.txAntenna;
            modelJson["ptr-start-chan"] = modelConfig.ptrStartChannel;
            modelJson["ptr-enable-chan"] = modelConfig.ptrEnableChannel;
            const auto power = cfg["power"].to<JsonObject>();
            power["max-power"] = modelConfig.power;
            power["dynamic-power"] = modelConfig.dynamicPower;
            power["boost-channel"] = modelConfig.boostChannel;
        }
    }
#endif /* TARGET_TX */

    if (!exportMode)
    {
        const auto settings = json["settings"].to<JsonObject>();
#if defined(TARGET_RX)
        cfg["serial-protocol"] = config.GetSerialProtocol();
        // NOTE: `serial1-protocol` is ESP32-only in devWIFI and is dropped here.
        cfg["sbus-failsafe"] = config.GetFailsafeMode();
        cfg["modelid"] = config.GetModelId();
        cfg["force-tlm"] = config.GetForceTlmOff();
        cfg["vbind"] = config.GetBindStorage();
        // All STM32 RX targets are GPIO_PIN_PWM_OUTPUTS_COUNT == 0, so this emits an
        // empty array and the Connections panel self-hides. The ESP feature-bit block
        // is dropped because it references ESP-only GPIO symbols.
        for (int ch = 0; ch < GPIO_PIN_PWM_OUTPUTS_COUNT; ++ch)
        {
            const auto channel = cfg["pwm"][ch].to<JsonObject>();
            channel["config"] = config.GetPwmChannel(ch)->raw;
            channel["pin"] = GPIO_PIN_PWM_OUTPUTS[ch];
            channel["features"] = 0;
        }
        if (GPIO_PIN_RCSIGNAL_RX != UNDEF_PIN && GPIO_PIN_RCSIGNAL_TX != UNDEF_PIN)
        {
            settings["has_serial_pins"] = true;
        }
#endif
        settings["product_name"] = product_name;
        settings["lua_name"] = device_name;
        settings["uidtype"] = getConfigUidType(json);
        // `ssid` dropped (no WiFi); `mode` reports the config transport instead.
        settings["mode"] = "USB";
        settings["custom_hardware"] = hardware_flag(HARDWARE_customised);
        settings["target"] = &target_name[4];
        settings["version"] = version;   // options.cpp: {LATEST_VERSION, 0}
        settings["git-commit"] = commit;
#if defined(TARGET_TX)
        settings["module-type"] = "TX";
#endif
#if defined(TARGET_RX)
        settings["module-type"] = "RX";
#endif
#if defined(RADIO_SX128X)
        settings["radio-type"] = "SX128X";
        settings["has_low_band"] = false;
        settings["has_high_band"] = true;
        settings["reg_domain_high"] = FHSSconfig->domain;
#elif defined(RADIO_SX127X)
        settings["radio-type"] = "SX127X";
        settings["has_low_band"] = true;
        settings["has_high_band"] = false;
        settings["reg_domain_low"] = FHSSconfig->domain;
#elif defined(RADIO_LR1121)
        settings["radio-type"] = "LR1121";
        settings["has_low_band"] = POWER_OUTPUT_VALUES_COUNT != 0;
        settings["has_high_band"] = POWER_OUTPUT_VALUES_DUAL_COUNT != 0;
        settings["reg_domain_low"] = FHSSconfig->domain;
        settings["reg_domain_high"] = FHSSconfigDualBand->domain;
#elif defined(RADIO_LR2021)
        settings["radio-type"] = "LR2021";
        settings["has_low_band"] = POWER_OUTPUT_VALUES_COUNT != 0;
        settings["has_high_band"] = POWER_OUTPUT_VALUES_DUAL_COUNT != 0;
        settings["reg_domain_low"] = FHSSconfig->domain;
        settings["reg_domain_high"] = FHSSconfigDualBand->domain;
#endif
    }
}

#if defined(TARGET_TX)
// ---------------------------------------------------------------------------
// Mirror of devWIFI.cpp::UpdateConfiguration() (TX) — button actions only.
// ---------------------------------------------------------------------------
static void applyButtonActions(JsonVariant json)
{
    if (json["button-actions"].is<JsonVariant>())
    {
        const JsonArray &array = json["button-actions"].as<JsonArray>();
        for (size_t button = 0; button < array.size(); button++)
        {
            tx_button_color_t action;
            for (int pos = 0; pos < (int)button_GetActionCnt(); pos++)
            {
                action.val.actions[pos].pressType = array[button]["action"][pos]["is-long-press"];
                action.val.actions[pos].count = array[button]["action"][pos]["count"];
                action.val.actions[pos].action = array[button]["action"][pos]["action"];
            }
            action.val.color = array[button]["color"];
            config.SetButtonActions(button, &action);
        }
    }
}

// ---------------------------------------------------------------------------
// Mirror of devWIFI.cpp::ImportConfiguration() (TX). This is the superset — a plain
// update posts only `button-actions` and every other block is simply absent.
// ---------------------------------------------------------------------------
const char *ConfigJson_ApplyConfig(JsonVariant json)
{
    if (json["config"].is<JsonVariant>())
    {
        json = json["config"];
    }

    if (json["fan-mode"].is<JsonVariant>()) config.SetFanMode(json["fan-mode"]);
    if (json["power-fan-threshold"].is<JsonVariant>()) config.SetPowerFanThreshold(json["power-fan-threshold"]);
    if (json["motion-mode"].is<JsonVariant>()) config.SetMotionMode(json["motion-mode"]);

    if (json["vtx-admin"].is<JsonObject>())
    {
        const auto vtxAdmin = json["vtx-admin"].as<JsonObject>();
        if (vtxAdmin["band"].is<JsonVariant>()) config.SetVtxBand(vtxAdmin["band"]);
        if (vtxAdmin["channel"].is<JsonVariant>()) config.SetVtxChannel(vtxAdmin["channel"]);
        if (vtxAdmin["pitmode"].is<JsonVariant>()) config.SetVtxPitmode(vtxAdmin["pitmode"]);
        if (vtxAdmin["power"].is<JsonVariant>()) config.SetVtxPower(vtxAdmin["power"]);
    }

    if (json["backpack"].is<JsonVariant>())
    {
        const auto backpack = json["backpack"].as<JsonObject>();
        if (backpack["disabled"].is<JsonVariant>()) config.SetBackpackDisable(backpack["disabled"]);
        if (backpack["dvr-start-delay"].is<JsonVariant>()) config.SetDvrStartDelay(backpack["dvr-start-delay"]);
        if (backpack["dvr-stop-delay"].is<JsonVariant>()) config.SetDvrStopDelay(backpack["dvr-stop-delay"]);
        if (backpack["dvr-aux-channel"].is<JsonVariant>()) config.SetDvrAux(backpack["dvr-aux-channel"]);
        if (backpack["telemetry-mode"].is<JsonVariant>()) config.SetBackpackTlmMode(backpack["telemetry-mode"]);
    }

    if (json["model"].is<JsonVariant>())
    {
        for (JsonPair kv : json["model"].as<JsonObject>())
        {
            const uint8_t model = atoi(kv.key().c_str());
            if (model >= CONFIG_TX_MODEL_CNT)
                continue;
            const auto modelJson = kv.value().as<JsonObject>();

            config.SetModelId(model);
            if (modelJson["packet-rate"].is<JsonVariant>()) config.SetRate(modelJson["packet-rate"]);
            if (modelJson["telemetry-ratio"].is<JsonVariant>()) config.SetTlm(modelJson["telemetry-ratio"]);
            if (modelJson["switch-mode"].is<JsonVariant>()) config.SetSwitchMode(modelJson["switch-mode"]);
            if (modelJson["link-mode"].is<JsonVariant>()) config.SetLinkMode(modelJson["link-mode"]);
            if (modelJson["model-match"].is<JsonVariant>()) config.SetModelMatch(modelJson["model-match"]);
            if (modelJson["tx-antenna"].is<JsonVariant>()) config.SetAntennaMode(modelJson["tx-antenna"]);
            if (modelJson["ptr-start-chan"].is<JsonVariant>()) config.SetPTRStartChannel(modelJson["ptr-start-chan"]);
            if (modelJson["ptr-enable-chan"].is<JsonVariant>()) config.SetPTREnableChannel(modelJson["ptr-enable-chan"]);
            if (modelJson["power"].is<JsonVariant>())
            {
                if (modelJson["power"]["max-power"].is<JsonVariant>()) config.SetPower(modelJson["power"]["max-power"]);
                if (modelJson["power"]["dynamic-power"].is<JsonVariant>()) config.SetDynamicPower(modelJson["power"]["dynamic-power"]);
                if (modelJson["power"]["boost-channel"].is<JsonVariant>()) config.SetBoostChannel(modelJson["power"]["boost-channel"]);
            }
            // have to commit after each model is updated
            config.Commit();
        }
        // As in devWIFI: the module is left on the last imported model. TxConfig has no
        // GetModelId() to restore from, and the handset re-selects the model on its next
        // model-ID frame anyway.
    }

    applyButtonActions(json);
    config.Commit();
    return nullptr;
}
#else /* TARGET_RX */
// ---------------------------------------------------------------------------
// Mirror of devWIFI.cpp::JsonUidToConfig()
// ---------------------------------------------------------------------------
static void jsonUidToConfig(JsonVariant json)
{
    const auto juid = json["uid"].as<JsonArray>();
    const size_t juidLen = juid.size() > (size_t)UID_LEN ? (size_t)UID_LEN : juid.size();
    uint8_t newUid[UID_LEN] = {0};

    // Copy only as many bytes as were included, right-justified
    // This supports 6-digit UID as well as 4-digit (OTA bound) UID
    copyArray(juid, &newUid[UID_LEN - juidLen], juidLen);

    if (memcmp(newUid, config.GetUID(), UID_LEN) != 0)
    {
        config.SetUID(newUid);
        config.Commit();
        // Also copy it to the global UID in case the page is reloaded
        memcpy(UID, newUid, UID_LEN);
    }
}

// ---------------------------------------------------------------------------
// Mirror of devWIFI.cpp::UpdateConfiguration() (RX)
// ---------------------------------------------------------------------------
const char *ConfigJson_ApplyConfig(JsonVariant json)
{
    if (json["config"].is<JsonVariant>())
    {
        json = json["config"];
    }

    const uint8_t protocol = json["serial-protocol"] | 0;
    config.SetSerialProtocol((eSerialProtocol)protocol);

    // `serial1-protocol` is ESP32-only in devWIFI and is dropped here.

    const uint8_t failsafe = json["sbus-failsafe"] | 0;
    config.SetFailsafeMode((eFailsafeMode)failsafe);

    long modelid = json["modelid"] | 255;
    if (modelid < 0 || modelid > 63) modelid = 255;
    config.SetModelId((uint8_t)modelid);

    const long forceTlm = json["force-tlm"] | false;
    config.SetForceTlmOff(forceTlm != 0);

    config.SetBindStorage((rx_config_bindstorage_t)(json["vbind"] | 0));
    if (json["uid"].is<JsonArray>())
    {
        jsonUidToConfig(json);
    }

    const JsonArray pwm = json["pwm"].as<JsonArray>();
    for (uint32_t channel = 0; channel < pwm.size(); channel++)
    {
        const uint32_t val = pwm[channel];
        config.SetPwmChannelRaw(channel, val);
    }

    config.Commit();
    return nullptr;
}
#endif

// ---------------------------------------------------------------------------
// Mirror of the config/model portion of devWIFI.cpp::HandleReset().
// The LittleFS removals (`hardware`, `lr1121`) have no STM32 equivalent; the `options` removal
// maps onto options_SetTrueDefaults(), which re-seeds the persisted blob from the flashed values.
// ---------------------------------------------------------------------------
void ConfigJson_Reset(const bool resetConfig, const bool resetOptions)
{
    if (resetOptions)
    {
#if defined(TARGET_RX)
        config.SetModelId(255);
        config.SetForceTlmOff(false);
        config.Commit();
#endif
        options_SetTrueDefaults();
    }
    if (resetConfig)
    {
        config.SetDefaults(true);
    }
}

#endif /* PLATFORM_STM32 */
