#if defined(TARGET_TX)

#include "USBProbe.h"
#include "handset.h"
#include "USBHandset.h"
#include "CRSFRouter.h"
#include "logging.h"
#include "helpers.h"
#include "devHandset.h"

extern Stream *TxUSB;

void USBProbe::Begin()
{
    port = TxUSB;
}

void USBProbe::End() {}

void USBProbe::handleInput()
{
    if (!port) return;
    int available = port->available();
    if (available <= 0) return;

    uint8_t buf[CRSF_MAX_PACKET_LEN];
    int toRead = std::min(available, (int)CRSF_MAX_PACKET_LEN);
    int n = port->readBytes(buf, toRead);
    if (n <= 0) return;

    processBytes(buf, n);
}

bool USBProbe::processBytes(const uint8_t *buf, int size)
{
    if (size <= 0) return false;

    bool committed = false;
    parser.processBytes(nullptr, buf, size, [this, &committed](const crsf_header_t *msg) {
        if (!committed && msg->type == CRSF_FRAMETYPE_RC_CHANNELS_PACKED)
        {
            committed = true;
            commitUSB();
        }
    });

    return committed;
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
