#include "usbcfg_framing.h"

#if defined(PLATFORM_STM32)

#include <string.h>

uint8_t usbcfg_crc8_dvb_s2(uint8_t crc, const uint8_t a)
{
    crc ^= a;
    for (int i = 0; i < 8; ++i)
    {
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
    }
    return crc;
}

bool UsbCfgParser::feed(const uint8_t b)
{
    switch (m_state)
    {
    case ST_IDLE:
        if (b == '$') m_state = ST_X;
        break;

    case ST_X:
        m_state = (b == 'X') ? ST_DIR : (b == '$' ? ST_X : ST_IDLE);
        break;

    case ST_DIR:
        // Only command frames are accepted from the host.
        m_state = (b == '<') ? ST_FLAGS : (b == '$' ? ST_X : ST_IDLE);
        break;

    case ST_FLAGS:
        m_flags = b;
        m_crc = usbcfg_crc8_dvb_s2(0, b);
        m_state = ST_FN_LO;
        break;

    case ST_FN_LO:
        m_function = b;
        m_crc = usbcfg_crc8_dvb_s2(m_crc, b);
        m_state = ST_FN_HI;
        break;

    case ST_FN_HI:
        m_function |= (uint16_t)b << 8;
        m_crc = usbcfg_crc8_dvb_s2(m_crc, b);
        m_state = ST_SIZE_LO;
        break;

    case ST_SIZE_LO:
        m_size = b;
        m_crc = usbcfg_crc8_dvb_s2(m_crc, b);
        m_state = ST_SIZE_HI;
        break;

    case ST_SIZE_HI:
        m_size |= (uint16_t)b << 8;
        m_crc = usbcfg_crc8_dvb_s2(m_crc, b);
        if (m_size > USBCFG_PAYLOAD_MAX)
        {
            // Oversized — cannot be one of ours, resynchronise.
            m_state = ST_IDLE;
            break;
        }
        m_offset = 0;
        m_state = m_size ? ST_PAYLOAD : ST_CRC;
        break;

    case ST_PAYLOAD:
        m_payload[m_offset++] = b;
        m_crc = usbcfg_crc8_dvb_s2(m_crc, b);
        if (m_offset >= m_size) m_state = ST_CRC;
        break;

    case ST_CRC:
        m_state = ST_IDLE;
        return b == m_crc;
    }
    return false;
}

void usbcfg_writeFrame(Stream *out, const char dir, const uint16_t function,
                       const uint8_t *payload, const uint16_t size)
{
    if (out == nullptr) return;

    uint8_t header[8];
    header[0] = '$';
    header[1] = 'X';
    header[2] = (uint8_t)dir;
    header[3] = 0;                          // flags
    header[4] = (uint8_t)(function & 0xFF);
    header[5] = (uint8_t)(function >> 8);
    header[6] = (uint8_t)(size & 0xFF);
    header[7] = (uint8_t)(size >> 8);

    uint8_t crc = 0;
    for (int i = 3; i < 8; ++i) crc = usbcfg_crc8_dvb_s2(crc, header[i]);
    for (uint16_t i = 0; i < size; ++i) crc = usbcfg_crc8_dvb_s2(crc, payload[i]);

    out->write(header, sizeof(header));
    if (size) out->write(payload, size);
    out->write(&crc, 1);
}

void usbcfg_writeAck(Stream *out, const uint16_t function)
{
    usbcfg_writeFrame(out, '>', function, nullptr, 0);
}

void usbcfg_writeError(Stream *out, const uint16_t function, const uint8_t errCode, const char *message)
{
    uint8_t buf[128];
    buf[0] = errCode;
    const size_t len = message ? strnlen(message, sizeof(buf) - 1) : 0;
    if (len) memcpy(&buf[1], message, len);
    usbcfg_writeFrame(out, '!', function, buf, (uint16_t)(len + 1));
}

// ---------------------------------------------------------------------------------------
// UsbCfgChunkWriter
// ---------------------------------------------------------------------------------------
void UsbCfgChunkWriter::flushChunk(const bool last)
{
    uint8_t header[7];
    uint8_t hdrLen = 0;
    header[hdrLen++] = (uint8_t)(m_seq & 0xFF);
    header[hdrLen++] = (uint8_t)(m_seq >> 8);
    uint8_t flags = 0;
    if (m_first) flags |= TLRS_CHUNK_FIRST;
    if (last)    flags |= TLRS_CHUNK_LAST;
    header[hdrLen++] = flags;
    if (m_first)
    {
        header[hdrLen++] = (uint8_t)(m_totalLen & 0xFF);
        header[hdrLen++] = (uint8_t)((m_totalLen >> 8) & 0xFF);
        header[hdrLen++] = (uint8_t)((m_totalLen >> 16) & 0xFF);
        header[hdrLen++] = (uint8_t)((m_totalLen >> 24) & 0xFF);
    }

    // Assemble header + data contiguously so the frame goes out as one write batch.
    uint8_t frame[7 + USBCFG_CHUNK_MAX];
    memcpy(frame, header, hdrLen);
    memcpy(frame + hdrLen, m_buf, m_fill);
    usbcfg_writeFrame(m_out, '>', m_function, frame, (uint16_t)(hdrLen + m_fill));

    m_seq++;
    m_first = false;
    m_fill = 0;
}

size_t UsbCfgChunkWriter::write(const uint8_t b)
{
    m_buf[m_fill++] = b;
    if (m_fill >= USBCFG_CHUNK_MAX) flushChunk(false);
    return 1;
}

size_t UsbCfgChunkWriter::write(const uint8_t *buffer, const size_t size)
{
    size_t remaining = size;
    while (remaining)
    {
        const size_t room = USBCFG_CHUNK_MAX - m_fill;
        const size_t take = remaining < room ? remaining : room;
        memcpy(&m_buf[m_fill], buffer, take);
        m_fill += take;
        buffer += take;
        remaining -= take;
        if (m_fill >= USBCFG_CHUNK_MAX) flushChunk(false);
    }
    return size;
}

void UsbCfgChunkWriter::finish()
{
    // Always emits at least one frame, so a zero-length document still terminates the stream.
    flushChunk(true);
}

#endif /* PLATFORM_STM32 */
