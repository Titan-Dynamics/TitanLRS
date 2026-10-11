#pragma once

#if defined(TARGET_TX)

#include <stdint.h>

#if !defined(TLRS_MAV_TX_COMPID)
#define TLRS_MAV_TX_COMPID 241 // MAV_COMP_ID_UART_BRIDGE
#endif

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
void TxMavlink_ForwardToGcs(const uint8_t *data, uint16_t size);
void TxMavlink_Update(uint32_t now);

#endif // TARGET_TX
