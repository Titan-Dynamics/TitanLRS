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

private:
    Stream *port = nullptr;
    CRSFParser parser;
    void commitUSB();
};

#endif // TARGET_TX
