#pragma once

#include "targets.h"

/*
 * RF diagnostics: bench tools served over the USB config API (lib/USBConfig, /rfdiag routes).
 *
 *   - RSSI scan: the radio sits in receive and its instantaneous RSSI is sampled about every
 *     millisecond, either on one channel or walking the bound FHSS hop sequence. Nothing is
 *     transmitted.
 *   - Continuous wave: an unmodulated carrier on one channel, at a power level from the
 *     hardware power table or at a raw radio output setting.
 *
 * Starting either one stops the RF link for good: the RF timer is stopped, the radio callbacks
 * are emptied and the connection state becomes rfDiag, which the TX and RX main loops leave
 * alone. The way back is a reboot (RfDiag_End()), as with the ESP web UI's CW test.
 *
 * Everything runs in the main loop (RfDiag_Service() from USBConfig_Poll()); with the link
 * stopped nothing else touches the radio, and the DIO interrupt glue is detached so a received
 * packet cannot start an SPI transaction of its own.
 */

#if defined(PLATFORM_STM32)

#include <ArduinoJson.h>

/** True when this build can run the tools (radio family supported). */
bool RfDiag_Supported();

/** True once a tool has been started (the link is down until reboot). */
bool RfDiag_Active();

/** GET /rfdiag: what the device can do (bands, channels, power levels) and the current state. */
void RfDiag_Describe(JsonObject out);

/**
 * POST /rfdiag: start a tool or change the running one. Returns nullptr on success, otherwise
 * an error message with `status` set to the HTTP status to answer with.
 */
const char *RfDiag_Apply(JsonVariantConst req, int &status);

/**
 * GET /rfdiag/samples: the RSSI samples from sequence number `from` on, as a JSON document in a
 * buffer owned by this module (valid until the next call). Returns its length.
 */
size_t RfDiag_Samples(uint32_t from, const char **json);

/** Leave the tools: save any pending config and reboot, which brings the link back. */
void RfDiag_End();

/** Run the active tool; call every main-loop iteration. */
void RfDiag_Service();

#endif
