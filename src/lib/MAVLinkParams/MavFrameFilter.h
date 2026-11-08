#pragma once

#include <stdint.h>

/*
 * Passes a byte stream on unchanged, apart from whole frames the owner chooses to remove. Each frame
 * is held until its CRC has been checked, so nothing of a removed frame gets out. Frames that fail
 * the CRC go through, because they may be from a dialect that isn't compiled in here.
 *
 * Include after the MAVLink dialect header.
 */

class MavFrameFilter
{
public:
    // Called for each frame with a good CRC. Returns true to remove it from the stream.
    typedef bool (*FrameFn)(const mavlink_message_t *msg, uint32_t now);
    // Called with whole frames, or with single bytes that are not part of a frame
    typedef void (*OutputFn)(const uint8_t *data, uint16_t len);

    void feed(const uint8_t *data, uint16_t size, uint32_t now, FrameFn onFrame, OutputFn output)
    {
        for (uint16_t i = 0; i < size; i++)
        {
            held_[heldLen_++] = data[i];
            mavlink_message_t msg;
            mavlink_status_t status;
            bool remove = false;
            if (mavlink_frame_char_buffer(&rxMsg_, &rxStatus_, data[i], &msg, &status) == MAVLINK_FRAMING_OK)
            {
                remove = onFrame(&msg, now);
            }
            if (rxStatus_.parse_state <= MAVLINK_PARSE_STATE_IDLE)
            {
                if (!remove)
                    output(held_, heldLen_);
                heldLen_ = 0;
            }
        }
        lastByteMs_ = now;
    }

    // Lets go of a frame whose remaining bytes never came, so the next frame is parsed as a frame
    void update(uint32_t now, uint32_t timeoutMs, OutputFn output)
    {
        if (rxStatus_.parse_state > MAVLINK_PARSE_STATE_IDLE && now - lastByteMs_ > timeoutMs)
        {
            output(held_, heldLen_);
            heldLen_ = 0;
            rxStatus_.parse_state = MAVLINK_PARSE_STATE_IDLE;
        }
    }

private:
    mavlink_message_t rxMsg_ = {};
    mavlink_status_t rxStatus_ = {};
    uint32_t lastByteMs_ = 0;
    // The parser is back to idle by the end of the longest frame
    uint8_t held_[MAVLINK_MAX_PACKET_LEN] = {};
    uint16_t heldLen_ = 0;
};
