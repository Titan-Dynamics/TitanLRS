#pragma once

#if defined(PLATFORM_STM32)

#include <Arduino.h>
#include <stdint.h>

#include "usbcfg_protocol.h"

// Payload buffer for one inbound frame. Must be >= USBCFG_CHUNK_MAX + the largest chunk header
// (resource 1 + seq 2 + flags 1 + totalLen 4 = 8).
#define USBCFG_PAYLOAD_MAX 1024

uint8_t usbcfg_crc8_dvb_s2(uint8_t crc, uint8_t a);

/**
 * @brief Incremental MSPv2 command-frame parser.
 *
 * Fed one byte at a time; returns true on the byte that completes a CRC-valid frame, at which
 * point function()/payload()/payloadSize() describe it until the next feed() call.
 *
 * The parser never blocks and never allocates — it is safe to run over bytes that are also
 * being forwarded to another consumer (this is exactly what the TX sniff mode does).
 */
class UsbCfgParser
{
public:
    void reset() { m_state = ST_IDLE; }
    bool feed(uint8_t b);

    uint16_t function() const { return m_function; }
    const uint8_t *payload() const { return m_payload; }
    uint16_t payloadSize() const { return m_size; }

private:
    enum State : uint8_t {
        ST_IDLE, ST_X, ST_DIR, ST_FLAGS, ST_FN_LO, ST_FN_HI,
        ST_SIZE_LO, ST_SIZE_HI, ST_PAYLOAD, ST_CRC
    };

    State    m_state = ST_IDLE;
    uint8_t  m_flags = 0;
    uint16_t m_function = 0;
    uint16_t m_size = 0;
    uint16_t m_offset = 0;
    uint8_t  m_crc = 0;
    uint8_t  m_payload[USBCFG_PAYLOAD_MAX];
};

/**
 * @brief Write one MSPv2 response frame.
 * @param dir '>' for success, '!' for error.
 */
void usbcfg_writeFrame(Stream *out, char dir, uint16_t function,
                       const uint8_t *payload, uint16_t size);

/** @brief Convenience: empty '>' acknowledgement. */
void usbcfg_writeAck(Stream *out, uint16_t function);

/** @brief Convenience: '!' error frame carrying errCode + ascii message. */
void usbcfg_writeError(Stream *out, uint16_t function, uint8_t errCode, const char *message);

/**
 * @brief Print adapter that streams bytes straight out as chunked response frames.
 *
 * Used to serialize a JsonDocument directly onto the wire without ever materialising the whole
 * document as text in RAM. Call finish() once serialization is done to flush the trailing chunk
 * with the LAST flag set (which may be a zero-length chunk if the document ended exactly on a
 * chunk boundary).
 */
class UsbCfgChunkWriter : public Print
{
public:
    UsbCfgChunkWriter(Stream *out, uint16_t function, uint32_t totalLen)
        : m_out(out), m_function(function), m_totalLen(totalLen) {}

    size_t write(uint8_t b) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    void finish();

private:
    void flushChunk(bool last);

    Stream   *m_out;
    uint16_t  m_function;
    uint32_t  m_totalLen;
    uint16_t  m_seq = 0;
    bool      m_first = true;
    uint16_t  m_fill = 0;
    uint8_t   m_buf[8 + USBCFG_CHUNK_MAX];
};

#endif /* PLATFORM_STM32 */
