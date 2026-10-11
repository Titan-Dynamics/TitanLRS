#pragma once

/*
 * Rebuilds a received frame exactly as it arrived, from the parser's message struct. Unlike
 * mavlink_msg_to_send_buffer(), the payload isn't re-trimmed: an untrimmed MAVLink 2 frame keeps
 * the length its checksum and signature cover.
 *
 * Include after the MAVLink dialect header.
 */

// hdr: at least MAVLINK_NUM_HEADER_BYTES. Returns the header length.
static inline uint8_t mavFrameHeader(const mavlink_message_t *msg, uint8_t *hdr)
{
    if (msg->magic == MAVLINK_STX_MAVLINK1)
    {
        hdr[0] = msg->magic;
        hdr[1] = msg->len;
        hdr[2] = msg->seq;
        hdr[3] = msg->sysid;
        hdr[4] = msg->compid;
        hdr[5] = msg->msgid & 0xFF;
        return MAVLINK_CORE_HEADER_MAVLINK1_LEN + 1;
    }
    hdr[0] = msg->magic;
    hdr[1] = msg->len;
    hdr[2] = msg->incompat_flags;
    hdr[3] = msg->compat_flags;
    hdr[4] = msg->seq;
    hdr[5] = msg->sysid;
    hdr[6] = msg->compid;
    hdr[7] = msg->msgid & 0xFF;
    hdr[8] = (msg->msgid >> 8) & 0xFF;
    hdr[9] = (msg->msgid >> 16) & 0xFF;
    return MAVLINK_NUM_HEADER_BYTES;
}

static inline uint8_t mavFrameSignatureLen(const mavlink_message_t *msg)
{
    return msg->magic == MAVLINK_STX && (msg->incompat_flags & MAVLINK_IFLAG_SIGNED) ? MAVLINK_SIGNATURE_BLOCK_LEN : 0;
}
