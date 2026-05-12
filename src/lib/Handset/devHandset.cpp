#include "targets.h"

#ifdef TARGET_TX

#include "CRSFHandset.h"
#include "POWERMGNT.h"
#include "devHandset.h"

#include "CRSFEndpoint.h"
#include "USBProbe.h"

#if defined(PLATFORM_ESP32)
#include "AutoDetect.h"
#endif

Handset *handset;
HandsetSource handsetSource = HANDSET_SOURCE_UNKNOWN;
USBProbe *usbProbe = nullptr;

static bool initialize()
{
#if defined(PLATFORM_ESP32)
    if (GPIO_PIN_RCSIGNAL_RX == GPIO_PIN_RCSIGNAL_TX)
        handset = new AutoDetect();
    else
#endif
        handset = new CRSFHandset();

    if (!firmwareOptions.is_airport)
        usbProbe = new USBProbe();

    return true;
}

static int start()
{
    handset->Begin();
#if defined(DEBUG_TX_FREERUN)
    handset->forceConnection();
#endif
    return DURATION_IMMEDIATELY;
}

static int timeout()
{
    handset->handleInput();
    return DURATION_IMMEDIATELY;
}

device_t Handset_device = {
    .initialize = initialize,
    .start = start,
    .event = nullptr,
    .timeout = timeout,
    .subscribe = EVENT_NONE,
};
#endif
