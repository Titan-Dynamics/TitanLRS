#include "devWIFI_STM32.h"
#include "ST67WifiOtaMode.h"
#include "devButton.h"
#include "config.h"
#include "hwTimer.h"
#include "logging.h"
#include "options.h"
#include "common.h"

static ST67WifiOtaMode wifiOtaMode;
static bool wifiStarted = false;
bool webserverPreventAutoStart = false;

static void forceSt67Off()
{
    pinMode(GPIO_PIN_ST67_BOOT, OUTPUT);
    digitalWrite(GPIO_PIN_ST67_BOOT, LOW);
    pinMode(GPIO_PIN_ST67_CHIP_EN, OUTPUT);
    digitalWrite(GPIO_PIN_ST67_CHIP_EN, LOW);
    pinMode(GPIO_PIN_ST67_CS, OUTPUT);
    digitalWrite(GPIO_PIN_ST67_CS, LOW);  // Active HIGH CS, LOW = deselected
}

void setWifiUpdateMode()
{
    setConnectionState(wifiUpdate);
}

static bool initialize()
{
    forceSt67Off();
    registerButtonFunction(ACTION_START_WIFI, setWifiUpdateMode);
    return true;
}

static int start()
{
    if (webserverPreventAutoStart || firmwareOptions.wifi_auto_on_interval == -1)
        return DURATION_NEVER;
    return firmwareOptions.wifi_auto_on_interval;
}

static int event()
{
    if (connectionState == wifiUpdate && !wifiStarted)
    {
        Serial.println(F("\n[WIFI_STM32] === WiFi mode triggered ==="));
        Serial.flush(); delay(10);

        Serial.println(F("[WIFI_STM32] Stopping hwTimer..."));
        Serial.flush(); delay(10);
        hwTimer::stop();

        Serial.println(F("[WIFI_STM32] Calling Radio.End()..."));
        Serial.flush(); delay(10);
        Radio.End();

        Serial.println(F("[WIFI_STM32] Starting WiFi OTA mode..."));
        Serial.flush(); delay(10);

        if (wifiOtaMode.start()) {
            wifiStarted = true;
            Serial.println(F("[WIFI_STM32] WiFi started OK"));
        } else {
            Serial.println(F("[WIFI_STM32] WiFi start FAILED"));
        }
        Serial.flush();
        return DURATION_IMMEDIATELY;
    }
    return DURATION_IGNORE;
}

static int timeout()
{
    if (wifiStarted)
    {
        wifiOtaMode.service();
        return 2;  // Service every 2ms
    }

    // Auto-on: if disconnected for wifi_auto_on_interval, trigger WiFi
    if (firmwareOptions.wifi_auto_on_interval != -1 && !webserverPreventAutoStart && connectionState == disconnected)
    {
        static bool pastAutoInterval = false;
        // If InBindingMode then wait at least 60 seconds before going into wifi,
        // regardless of if .wifi_auto_on_interval is set to less
        if (!InBindingMode || firmwareOptions.wifi_auto_on_interval >= 60000 || pastAutoInterval)
        {
            setWifiUpdateMode();
            return DURATION_IMMEDIATELY;
        }
        pastAutoInterval = true;
        return (60000 - firmwareOptions.wifi_auto_on_interval);
    }
    return DURATION_NEVER;
}

device_t WIFI_STM32_device = {
    .initialize = initialize,
    .start = start,
    .event = event,
    .timeout = timeout,
    .subscribe = EVENT_CONNECTION_CHANGED
};
