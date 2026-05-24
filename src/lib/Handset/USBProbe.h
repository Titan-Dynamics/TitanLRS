#pragma once

#if defined(TARGET_TX)

#include "crsf_protocol.h"

class USBProbe
{
public:
    bool processBytes(const uint8_t *buf, int size);

private:
    uint8_t frame[CRSF_MAX_PACKET_LEN] {};
    uint8_t frameDataIndex = 0;
    uint8_t expectedFrameSize = 0;

    enum ProbeState : uint8_t
    {
        PROBE_IDLE,
        PROBE_RECEIVING_LENGTH,
        PROBE_RECEIVING_DATA,
    } state = PROBE_IDLE;

    bool processByte(uint8_t byte);
    void commitUSB();
};

#endif // TARGET_TX
