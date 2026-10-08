#include "USBConfigConnector.h"

#if defined(PLATFORM_STM32)

#include "usbcfg_framing.h"

void USBConfigConnector::forwardMessage(const crsf_header_t *message)
{
    if (m_port == nullptr) return;

    // Only LUA-relevant frames go up the pipe, mirroring TXUSBConnector's backpack filter. The
    // browser has no use for telemetry or RC frames, and letting them through would put the config
    // interface under continuous load for nothing.
    switch (message->type)
    {
    case CRSF_FRAMETYPE_PARAMETER_SETTINGS_ENTRY:
    case CRSF_FRAMETYPE_PARAMETER_READ:
    case CRSF_FRAMETYPE_PARAMETER_WRITE:
    case CRSF_FRAMETYPE_DEVICE_INFO:
    case CRSF_FRAMETYPE_DEVICE_PING:
    case CRSF_FRAMETYPE_ELRS_STATUS:
        break;
    default:
        return;
    }

    const uint8_t length = message->frame_size + CRSF_FRAME_NOT_COUNTED_BYTES;
    usbcfg_writeFrame(m_port, '>', TLRS_CRSF, (const uint8_t *)message, length);
}

#endif /* PLATFORM_STM32 */
