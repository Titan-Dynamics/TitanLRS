#pragma once

#if defined(TARGET_TX)

#include "CRSFParser.h"
#include "crsf_protocol.h"

class USBProbe
{
public:
    void Begin();
    void End();
    void handleInput();
    bool processBytes(const uint8_t *buf, int size);

private:
    Stream *port = nullptr;
    CRSFParser parser;
    void commitUSB();
};

#endif // TARGET_TX
