#include "TXUSBConnector.h"

#include "common.h"
#include "config.h"

extern Stream *BackpackOrLogStrm;
extern Stream *TxUSB;

void TXUSBConnector::forwardMessage(const crsf_header_t *message)
{
    // In MAVLink mode the USB port carries the raw MAVLink stream to a GCS. CRSF frames written
    // into it (link statistics every tlm-interval) land between the downlink chunks a MAVLink
    // frame is split over, so the GCS sees CRC failures and counts lost packets on a perfect
    // link. A GCS has no use for them. (The backpack branch below is unaffected: it is a
    // separate consumer of LUA frames, not the GCS's stream.)
    if (TxUSB != BackpackOrLogStrm && config.GetLinkMode() != TX_MAVLINK_MODE)
    {
        const size_t length = message->frame_size + CRSF_FRAME_NOT_COUNTED_BYTES;
        TxUSB->write((uint8_t *)message, length);
    }

    if (TxUSB == BackpackOrLogStrm)
    {
        // Only send LUA relevant messages to the backpack
        if (message->type == CRSF_FRAMETYPE_PARAMETER_SETTINGS_ENTRY ||
            message->type == CRSF_FRAMETYPE_PARAMETER_READ ||
            message->type == CRSF_FRAMETYPE_PARAMETER_WRITE ||
            message->type == CRSF_FRAMETYPE_DEVICE_INFO ||
            message->type == CRSF_FRAMETYPE_DEVICE_PING ||
            message->type == CRSF_FRAMETYPE_ELRS_STATUS)
        {
            const uint8_t length = message->frame_size + CRSF_FRAME_NOT_COUNTED_BYTES;
            TxUSB->write((uint8_t *)message, length);
        }
    }
}

uint8_t TXUSBConnector::GetMaxPacketBytes() const
{
#if defined(PLATFORM_ESP32_S3)
#if ESP_ARDUINO_VERSION <= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    // Arduino < 3.0.0 & S3 has real issues sending 64 bytes!
    return CRSF_MAX_PACKET_LEN >= 64 ? 63 : CRSF_MAX_PACKET_LEN;
#else
    return CRSF_MAX_PACKET_LEN;
#endif
#else
    return CRSF_MAX_PACKET_LEN;
#endif
}
