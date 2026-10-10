#pragma once

#if defined(PLATFORM_STM32)

#include "device.h"

/*
 * The USB config service: the HTTP API in usbcfg_api.h, served on the USB network interface
 * (lib/USBNet). Registering the device brings the network up; it is registered in both the normal
 * and the "no hardware layout" startup paths so a misconfigured board can still be fixed or
 * reflashed.
 */
extern device_t USBConfig_device;

/**
 * @brief Service the USB network and the config API; call once per main-loop iteration.
 *
 * Runs lwIP (received frames, timers, transmissions) and answers a waiting CRSF long-poll once
 * there is something to send. Everything network-related happens here, in the main loop — never
 * in an interrupt.
 */
void USBConfig_Poll();

#endif /* PLATFORM_STM32 */
