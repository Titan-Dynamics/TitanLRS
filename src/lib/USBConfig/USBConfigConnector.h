#pragma once

#if defined(PLATFORM_STM32)

#include <Arduino.h>

#include "CRSFConnector.h"

/**
 * @brief A CRSFRouter connector that tunnels CRSF frames to the web dashboard over HTTP.
 *
 * This is the firmware half of the web dashboard's Parameters tab: the browser speaks the whole
 * CRSF parameter protocol itself (device ping, sequential parameter enumeration, chunk
 * reassembly, writes), and this connector is the dumb pipe that carries the frames — exactly
 * the role the Backpack's /crsf WebSocket plays on an ESP TX.
 *
 * Browsers only get plain fetch() to a private address (Local Network Access does not cover
 * WebSockets), so frames for the browser are queued here and collected by a long-polled
 * `GET /crsf`; frames from the browser arrive by `POST /crsf` (devUSBConfig.cpp).
 *
 * It lives in lib/USBConfig rather than lib/tx-crsf because it serves both targets: it only knows
 * about the global crsfRouter, and lib/tx-crsf is not part of an RX build.
 *
 * Lifetime: devUSBConfig registers this with the router on the first /crsf request and removes it
 * once the browser stops polling, so the router is not feeding a queue nobody is reading.
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

    bool hasFrames() const { return m_head != m_tail; }

    /** Move as many whole queued frames as fit into `out`; returns the byte count. */
    size_t drain(uint8_t *out, size_t outLen);

    void clear() { m_head = m_tail = 0; }

private:
    // A parameter-tree enumeration answers one chunk per request, so the queue only has to ride
    // out the gap between two polls.
    static constexpr size_t QUEUE_SIZE = 2048;
    uint8_t m_queue[QUEUE_SIZE];
    size_t m_head = 0; // next byte to read
    size_t m_tail = 0; // next byte to write

    size_t used() const { return (m_tail + QUEUE_SIZE - m_head) % QUEUE_SIZE; }
    uint8_t at(size_t offset) const { return m_queue[(m_head + offset) % QUEUE_SIZE]; }
};

#endif /* PLATFORM_STM32 */
