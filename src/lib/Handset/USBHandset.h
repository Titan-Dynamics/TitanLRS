#pragma once

#if defined(TARGET_TX)

#include "handset.h"
#include "CRSFConnector.h"
#include "CRSFParser.h"
#include "crsf_protocol.h"

class USBHandset final : public Handset, public CRSFConnector
{
public:
    void Begin() override;
    void End() override;

    // Handset
    void handleInput() override;
    void setPacketInterval(int32_t PacketInterval) override;
    void JustSentRFpacket() override;
    uint8_t GetMaxPacketBytes() const override { return CRSF_MAX_PACKET_LEN; }
    int getMinPacketInterval() const override { return 1; }

    // CRSFConnector
    void forwardMessage(const crsf_header_t *message) override;

private:
    Stream *port = nullptr;
    CRSFParser parser;

    // OpenTX mixer sync
    volatile uint32_t dataLastRecv = 0;
    volatile int32_t  OpenTXsyncOffset = 0;
    volatile int32_t  OpenTXsyncWindow = 0;
    volatile int32_t  OpenTXsyncWindowSize = 0;
    uint32_t          OpenTXsyncLastSent = 0;

    // Packet stats (latched into CRSFHandset::GoodPktsCountResult / BadPktsCountResult each second)
    uint32_t GoodPktsCount = 0;
    uint32_t BadPktsCount = 0;
    uint32_t pktStatsLastLatched = 0;
    static constexpr uint32_t PKT_STATS_LATCH_INTERVAL_MS = 1000;

    // Connection watchdog
    uint32_t lastRxMillis = 0;
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 1000;

    void sendSyncPacketToTX();
};

#endif // TARGET_TX
