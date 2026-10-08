#pragma once

#if defined(PLATFORM_STM32)

#include <Arduino.h>

#include "CRSFConnector.h"

/**
 * @brief A CRSFRouter connector that tunnels CRSF frames over the USB config pipe.
 *
 * This is the firmware half of the web dashboard's Parameters tab: the browser speaks the whole
 * CRSF parameter protocol itself (device ping, sequential parameter enumeration, chunk
 * reassembly, writes), and this connector is the dumb byte pipe that carries the frames — exactly
 * the role the Backpack's /crsf WebSocket plays on an ESP TX.
 *
 * It lives in lib/USBConfig rather than lib/tx-crsf because it serves both targets: it only knows
 * about a Stream and the global crsfRouter, and lib/tx-crsf is not part of an RX build.
 *
 * Lifetime is the config session: devUSBConfig registers this with the router on TLRS_HELLO and
 * removes it on TLRS_BYE or session timeout, so the router never writes into a pipe with no
 * reader on the other end.
 */
class USBConfigConnector final : public CRSFConnector
{
public:
    void forwardMessage(const crsf_header_t *message) override;

    /**
     * The CRSF length field is a single byte, so nothing is gained by advertising more than a
     * maximum-size CRSF frame; this is what CRSFEndpoint uses to size parameter chunks.
     */
    uint8_t GetMaxPacketBytes() const override { return CRSF_MAX_PACKET_LEN; }

    /** Set (or clear, with nullptr) the pipe frames are written to. */
    void setPort(Stream *port) { m_port = port; }

private:
    Stream *m_port = nullptr;
};

#endif /* PLATFORM_STM32 */
