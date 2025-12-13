#include "TXBackpackConnector.h"

#include "config.h"

extern Stream *BackpackOrLogStrm;
extern Stream *TxUSB;

void TXBackpackConnector::forwardMessage(const crsf_header_t *message)
{
    if (TxUSB != BackpackOrLogStrm)
    {
        const uint8_t length = message->frame_size + CRSF_FRAME_NOT_COUNTED_BYTES;
        BackpackOrLogStrm->write((uint8_t *)message, length);
    }
}
