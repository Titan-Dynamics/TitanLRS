#pragma once

#include "SerialIO.h"
#include "FIFO.h"

#define MAV_INPUT_BUF_LEN       1024
#define MAV_OUTPUT_BUF_LEN      512
#define MAV_PAYLOAD_SIZE_MAX    60
#define MAV_MAX_FRAME_LEN       280 // MAVLINK_MAX_PACKET_LEN: v2 header + 255 byte payload + CRC + signature

// Variables / constants
extern FIFO<MAV_INPUT_BUF_LEN> mavlinkInputBuffer;
extern FIFO<MAV_OUTPUT_BUF_LEN> mavlinkOutputBuffer;

class SerialMavlink final : public SerialIO {
public:
    explicit SerialMavlink(Stream &out, Stream &in);
    ~SerialMavlink() override;

    uint32_t sendRCFrame(bool frameAvailable, bool frameMissed, uint32_t *channelData) override;

    int getMaxSerialReadSize() override;
    void sendQueuedData(uint32_t maxBytesToSend) override;

    void forwardMessage(const uint8_t *data) override;
    bool GetNextPayload(uint8_t *nextPayloadSize, uint8_t *payloadData) override;

    void event() override;

private:
    void processBytes(uint8_t *bytes, u_int16_t size) override;
    void learnFromFcFrame();

    uint8_t this_system_id;
    const uint8_t this_component_id;

    uint8_t target_system_id;
    const uint8_t target_component_id;

    uint32_t lastSentFlowCtrl = 0;

    // FC frames are queued whole, so the receiver's own frames always land between them
    uint8_t fcFrame[MAV_MAX_FRAME_LEN];
    uint16_t fcFrameLen = 0;
    uint16_t fcFrameExpectedLen = 0;

    // Variables / constants for Mavlink //
    FIFO<MAV_INPUT_BUF_LEN> mavlinkInputBuffer;
    FIFO<MAV_OUTPUT_BUF_LEN> mavlinkOutputBuffer;
};
