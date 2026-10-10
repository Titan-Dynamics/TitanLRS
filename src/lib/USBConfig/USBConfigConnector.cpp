#include "USBConfigConnector.h"

#if defined(PLATFORM_STM32)

void USBConfigConnector::forwardMessage(const crsf_header_t *message)
{
    // Only LUA-relevant frames go to the browser, mirroring TXUSBConnector's backpack filter. The
    // browser has no use for telemetry or RC frames, and letting them through would only fill the
    // queue between polls.
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

    const size_t length = message->frame_size + CRSF_FRAME_NOT_COUNTED_BYTES;
    // Keep one byte free so a full queue is distinguishable from an empty one. A frame that does
    // not fit is dropped whole; the browser's per-parameter retry asks again.
    if (used() + length >= QUEUE_SIZE)
    {
        return;
    }
    const uint8_t *bytes = (const uint8_t *)message;
    for (size_t i = 0; i < length; ++i)
    {
        m_queue[m_tail] = bytes[i];
        m_tail = (m_tail + 1) % QUEUE_SIZE;
    }
}

size_t USBConfigConnector::drain(uint8_t *out, size_t outLen)
{
    size_t written = 0;
    while (used() >= 2)
    {
        // Every queued frame is whole: sync/address byte, then frame_size counting the rest.
        const size_t length = at(1) + CRSF_FRAME_NOT_COUNTED_BYTES;
        if (written + length > outLen)
        {
            break;
        }
        for (size_t i = 0; i < length; ++i)
        {
            out[written++] = m_queue[m_head];
            m_head = (m_head + 1) % QUEUE_SIZE;
        }
    }
    return written;
}

#endif /* PLATFORM_STM32 */
