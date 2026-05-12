#if defined(TARGET_TX)

#include "USBProbe.h"
#include "handset.h"
#include "USBHandset.h"
#include "CRSFRouter.h"
#include "logging.h"
#include "helpers.h"
#include "devHandset.h"

extern Stream *TxUSB;

bool USBProbe::processBytes(const uint8_t *buf, int size)
{
    if (size <= 0) return false;

    for (int i = 0; i < size; ++i)
    {
        if (processByte(buf[i]))
            return true;
    }

    return false;
}

bool USBProbe::processByte(uint8_t byte)
{
    switch (state)
    {
    case PROBE_IDLE:
        if (byte == CRSF_SYNC_BYTE || byte == CRSF_ADDRESS_RADIO_TRANSMITTER || byte == CRSF_ADDRESS_CRSF_RECEIVER)
        {
            frame[0] = byte;
            state = PROBE_RECEIVING_LENGTH;
        }
        return false;

    case PROBE_RECEIVING_LENGTH:
        if (byte < (CRSF_MIN_PACKET_LEN - CRSF_FRAME_NOT_COUNTED_BYTES) ||
            byte > (CRSF_MAX_PACKET_LEN - CRSF_FRAME_NOT_COUNTED_BYTES))
        {
            state = PROBE_IDLE;
            return false;
        }

        frame[CRSF_TELEMETRY_LENGTH_INDEX] = byte;
        expectedFrameSize = byte;
        frameDataIndex = 0;
        state = PROBE_RECEIVING_DATA;
        return false;

    case PROBE_RECEIVING_DATA:
        frame[frameDataIndex + CRSF_FRAME_NOT_COUNTED_BYTES] = byte;
        frameDataIndex++;

        if (frameDataIndex != expectedFrameSize)
            return false;

        state = PROBE_IDLE;

        const uint8_t crc = crsfRouter.crsf_crc.calc(
            frame + CRSF_FRAME_NOT_COUNTED_BYTES,
            frame[CRSF_TELEMETRY_LENGTH_INDEX] - CRSF_TELEMETRY_CRC_LENGTH);

        if (byte != crc)
            return false;

        const auto *msg = (const crsf_header_t *)frame;
        if (msg->type != CRSF_FRAMETYPE_RC_CHANNELS_PACKED)
            return false;

        commitUSB();
        return true;
    }

    return false;
}

void USBProbe::commitUSB()
{
    DBGLN("USB CRSF handset detected");
    handsetSource = HANDSET_SOURCE_USB;

    handset->End();
    Handset *oldHandset = handset;

    USBHandset *usb = new USBHandset();
    oldHandset->migrateCallbacksTo(usb);
    usb->Begin();
    handset = usb;
    delete oldHandset;

    if (BackpackOrLogStrm == TxUSB)
        BackpackOrLogStrm = new NullStream();

    usbProbe = nullptr; // signal to caller that commit occurred; caller must delete us
}

#endif // TARGET_TX
