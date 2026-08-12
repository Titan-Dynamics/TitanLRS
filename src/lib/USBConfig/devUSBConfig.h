#pragma once

#if defined(PLATFORM_STM32)

#include "device.h"
#include <stdint.h>

extern device_t USBConfig_device;

/**
 * @brief Feed USB CDC bytes to the config service.
 *
 * While no session is open this is a *non-destructive sniff*: the bytes are scanned for a
 * complete, CRC-valid TCFG_HELLO frame and nothing else happens, so the caller must keep
 * forwarding them to their normal consumers (MAVLink auto-detect / uplink, CRSF parser).
 *
 * Once a session is open the config service owns the stream exclusively.
 *
 * @return true when the config service has consumed these bytes and the caller must NOT pass
 *         them on to any other consumer.
 */
bool USBConfig_ProcessBytes(const uint8_t *buf, uint16_t len);

/**
 * @brief True while a config session is open.
 *
 * The TX uses this to pause MAVLink-over-USB forwarding for the duration of the session. The
 * session ends on TCFG_BYE or after USBCFG_SESSION_TIMEOUT_MS without a valid frame, so a
 * yanked cable cannot leave MAVLink muted.
 */
bool USBConfig_SessionActive();

#if defined(TARGET_RX)
/**
 * @brief Drain the CDC port into the config service. RX only; call once per main-loop iteration.
 *
 * The RX owns the CDC port outright (unlike the TX, where tx_main reads it for MAVLink and feeds
 * us via USBConfig_ProcessBytes), so someone has to read it. This must be driven from the main
 * loop rather than from the device timeout hook: at 64 bytes per USB frame the port can deliver
 * far more than the CDC receive queue holds inside one 10 ms hook interval, and this core never
 * recovers once that queue fills — it stops re-arming the OUT endpoint for good (Issues.md
 * BUG #5). Draining every iteration keeps the queue shallow, which is exactly why the TX side
 * never hit it.
 */
void USBConfig_DrainPort();
#endif

#endif /* PLATFORM_STM32 */
