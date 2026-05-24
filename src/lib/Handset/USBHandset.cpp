#if defined(TARGET_TX)

#include "USBHandset.h"
#include "CRSFHandset.h"
#include "CRSFRouter.h"
#include "helpers.h"

extern Stream *TxUSB;

static constexpr int32_t OpenTXsyncPacketInterval = 200; // ms
static constexpr int32_t OpenTXsyncOffsetSafeMargin = 1000; // 100us in 10ths of us

void USBHandset::Begin()
{
    port = TxUSB;

    addDevice(CRSF_ADDRESS_ELRS_LUA);
    addDevice(CRSF_ADDRESS_RADIO_TRANSMITTER);
    crsfRouter.addConnector(this);

    OpenTXsyncWindowSize = std::max((int32_t)1, (int32_t)(20000 / RequestedRCpacketInterval));
}

void USBHandset::End()
{
    crsfRouter.removeConnector(this);
}

void USBHandset::forwardMessage(const crsf_header_t *message)
{
    if (!controllerConnected)
        return;

    uint8_t size = CRSF_FRAME_SIZE(message->frame_size);
    if (size > CRSF_MAX_PACKET_LEN)
        return;

    auto *data = (uint8_t *)message;
    data[0] = CRSF_SYNC_BYTE;
    port->write(data, size);
}

void USBHandset::handleInput()
{
    // Drain available bytes and feed them into the parser
    auto available = port->available();
    if (available > 0)
    {
        uint8_t buf[CRSF_MAX_PACKET_LEN];
        int toRead = std::min((int)available, (int)CRSF_MAX_PACKET_LEN);
        int n = port->readBytes(buf, toRead);
        if (n > 0)
        {
            parser.processBytes(this, buf, n,
                [this](const crsf_header_t *msg) {
                    GoodPktsCount++;
                    dataLastRecv = micros();
                    lastRxMillis = millis();
                    if (!controllerConnected)
                    {
                        controllerConnected = true;
                        if (connected) connected();
                    }
                    crsfRouter.processMessage(this, msg);
                },
                [this]() {
                    BadPktsCount++;
                });
        }
    }

    // Send sync packet if connected and rate-limit has elapsed
    if (controllerConnected)
    {
        sendSyncPacketToTX();
    }

    // Latch packet stats for LUA polling (mirrors CRSFHandset::UARTwdt behaviour)
    {
        const uint32_t now = millis();
        if (now - pktStatsLastLatched >= PKT_STATS_LATCH_INTERVAL_MS)
        {
            CRSFHandset::GoodPktsCountResult = GoodPktsCount;
            CRSFHandset::BadPktsCountResult  = BadPktsCount;
            GoodPktsCount = 0;
            BadPktsCount  = 0;
            pktStatsLastLatched = now;
        }
    }

    // Disconnection watchdog
    if (controllerConnected && (millis() - lastRxMillis > CONNECT_TIMEOUT_MS))
    {
        controllerConnected = false;
        if (disconnected) disconnected();
    }
}

void ICACHE_RAM_ATTR USBHandset::setPacketInterval(int32_t PacketInterval)
{
    RequestedRCpacketInterval = PacketInterval;
    OpenTXsyncOffset = 0;
    OpenTXsyncWindow = 0;
    OpenTXsyncWindowSize = std::max((int32_t)1, (int32_t)(20000 / RequestedRCpacketInterval));
    OpenTXsyncLastSent -= OpenTXsyncPacketInterval;
}

void ICACHE_RAM_ATTR USBHandset::JustSentRFpacket()
{
    uint32_t last = dataLastRecv;
    uint32_t m = micros();
    auto delta = (int32_t)(m - last);

    if (delta >= (int32_t)RequestedRCpacketInterval)
    {
        OpenTXsyncOffset = -(delta % RequestedRCpacketInterval) * 10;
        OpenTXsyncWindow = 0;
        OpenTXsyncLastSent -= OpenTXsyncPacketInterval;
    }
    else
    {
        OpenTXsyncWindow = std::min(OpenTXsyncWindow + 1, (int32_t)OpenTXsyncWindowSize);
        OpenTXsyncOffset = ((OpenTXsyncOffset * (OpenTXsyncWindow - 1)) + delta * 10) / OpenTXsyncWindow;
    }
}

void USBHandset::sendSyncPacketToTX()
{
    const uint32_t now = millis();
    if (now - OpenTXsyncLastSent >= (uint32_t)OpenTXsyncPacketInterval)
    {
        int32_t packetRate = RequestedRCpacketInterval * 10;
        int32_t offset = OpenTXsyncOffset - OpenTXsyncOffsetSafeMargin;

        CRSF_MK_EXT_FRAME_T(crsf_sync_packet_t) sync_packet = {
            .h = {
                CRSF_ADDRESS_RADIO_TRANSMITTER,
                CRSF_EXT_FRAME_SIZE(sizeof(crsf_sync_packet_t)),
                CRSF_FRAMETYPE_HANDSET,
                CRSF_ADDRESS_RADIO_TRANSMITTER,
                CRSF_ADDRESS_CRSF_TRANSMITTER,
            },
            .p = {
                .subType = CRSF_HANDSET_SUBCMD_TIMING,
                .rate = htobe32(packetRate),
                .offset = htobe32(offset)
            },
            .crc = crsfRouter.crsf_crc.calc((uint8_t *)&sync_packet + CRSF_TELEMETRY_TYPE_INDEX, sizeof(sync_packet) - 3)
        };
        crsfRouter.deliverMessageTo(CRSF_ADDRESS_RADIO_TRANSMITTER, (crsf_header_t *)&sync_packet);

        OpenTXsyncLastSent = now;
    }
}

#endif // TARGET_TX
