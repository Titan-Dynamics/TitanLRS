#pragma once

#if defined(PLATFORM_STM32)

#include "device.h"
#include <stdint.h>

extern device_t USBConfig_device;

/**
 * @brief Feed config-pipe bytes to the config service.
 *
 * The service owns the vendor-class config pipe (SerialCfg, lib/USBComposite) outright on both TX
 * and RX, so these bytes have no other consumer. A session opens on a CRC-valid TCFG_HELLO and
 * closes on TCFG_BYE or USBCFG_SESSION_TIMEOUT_MS of silence.
 *
 * @return true while a session is open.
 */
bool USBConfig_ProcessBytes(const uint8_t *buf, uint16_t len);

/**
 * @brief True while a config session is open.
 */
bool USBConfig_SessionActive();

/**
 * @brief Drain the config pipe into the config service; call once per main-loop iteration.
 *
 * This must be driven from the main loop rather than from the device timeout hook: at 64 bytes per
 * USB frame the pipe can deliver far more than the receive queue holds inside one 10 ms hook
 * interval, and this core never recovers once that queue fills — it stops re-arming the OUT
 * endpoint for good (Issues.md BUG #5). Draining every iteration keeps the queue shallow.
 */
void USBConfig_DrainPort();

#endif /* PLATFORM_STM32 */
