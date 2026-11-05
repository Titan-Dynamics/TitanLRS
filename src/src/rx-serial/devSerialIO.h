#pragma once

#include "device.h"

extern device_t Serial0_device;
#if defined(PLATFORM_ESP32) || defined(PLATFORM_STM32)
extern device_t Serial1_device;
#endif
extern void sendImmediateRC();
extern void handleSerialIO();
extern void crsfRCFrameAvailable();
extern void crsfRCFrameMissed();
