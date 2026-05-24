#pragma once

#include "device.h"

extern device_t Handset_device;

#ifdef TARGET_TX

enum HandsetSource : uint8_t
{
    HANDSET_SOURCE_UNKNOWN,
    HANDSET_SOURCE_UART,
    HANDSET_SOURCE_USB
};

extern HandsetSource handsetSource;

class USBProbe;
extern USBProbe *usbProbe;

#endif // TARGET_TX