#pragma once

#if defined(TARGET_TX)

#include <stddef.h>
#include <stdint.h>

#if !defined(TLRS_MAV_TX_COMPID)
#define TLRS_MAV_TX_COMPID 241 // MAV_COMP_ID_UART_BRIDGE
#endif

// The most a GCS link can hold back from the uplink: one frame, MAVLINK_MAX_PACKET_LEN
#define TX_MAVLINK_HELD_FRAME_LEN 280

// Each GCS link gets its own parser, so frames arriving on several at once are not mixed together
enum txMavlinkGcsLink_e : uint8_t
{
    GCS_LINK_USB_SERIAL,
#if defined(PLATFORM_STM32)
    GCS_LINK_UDP,
#endif
#if defined(PLATFORM_ESP32)
    GCS_LINK_BACKPACK,
#endif
    GCS_LINK_COUNT
};

void TxMavlink_ProcessFromGcs(txMavlinkGcsLink_e link, const uint8_t *data, uint16_t size);
void TxMavlink_QueueUplink(const uint8_t *data, uint16_t size);
void TxMavlink_OverrideChannels(uint32_t channels[], size_t channelCount);
// No handset: true while the GCS joystick has channels to send (written to channels), false while it has none
bool TxMavlink_GcsOnlyAllowed();
bool TxMavlink_GcsOnlyChannels(uint32_t channels[], size_t channelCount, uint32_t now);
void TxMavlink_ForwardToGcs(const uint8_t *data, uint16_t size);
void TxMavlink_Update(uint32_t now);

#endif // TARGET_TX
