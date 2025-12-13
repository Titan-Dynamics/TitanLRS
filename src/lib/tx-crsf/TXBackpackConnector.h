#ifndef TX_BACKPACK_CONNECTOR_H
#define TX_BACKPACK_CONNECTOR_H
#include "CRSFConnector.h"

class TXBackpackConnector final : public CRSFConnector
{
public:
    void forwardMessage(const crsf_header_t *message) override;
};

#endif //TX_BACKPACK_CONNECTOR_H
