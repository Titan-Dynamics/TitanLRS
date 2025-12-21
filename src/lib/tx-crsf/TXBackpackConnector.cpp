#include "TXBackpackConnector.h"
#include "devBackpack.h"

#include "config.h"

extern Stream *BackpackOrLogStrm;
extern Stream *TxUSB;

void TXBackpackConnector::forwardMessage(const crsf_header_t *message)
{
    if (TxUSB != BackpackOrLogStrm)
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
            BackpackOrLogStrm->write((uint8_t *)message, length);
        }
    }
}
