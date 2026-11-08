#pragma once

#include <stddef.h>
#include <stdint.h>

#include "crsf_protocol.h"

/*
 * RC_CHANNELS_OVERRIDE from a GCS (e.g. the Mission Planner joystick), laid over the handset's
 * channels before they go over the air. Only the channels in the forward mask are taken from the
 * GCS, everything else stays with the handset. The handset's enable channel hands them over:
 * while it is high the handset has no effect on them, and they hold where they were until the
 * GCS sets them. The enable channel itself is never taken from the GCS.
 *
 * With no handset at all, fillAll() gives the channels in the mask from the GCS and every other
 * channel low, as long as the GCS keeps sending.
 *
 * Include after the MAVLink dialect header.
 */

class MavRcOverride
{
public:
    static constexpr uint8_t CHANNELS = 16;

    // Returns true if the message is an override for this vehicle, allowed or not. These must not
    // reach the flight controller, which would act on them with no regard for the handset, and
    // mix them with the overrides the RX sends it.
    // vehicleSysid is 0 until the vehicle has been seen.
    bool handleMessage(const mavlink_message_t *msg, uint8_t vehicleSysid, uint32_t now)
    {
        if (msg->msgid != MAVLINK_MSG_ID_RC_CHANNELS_OVERRIDE)
            return false;

        mavlink_rc_channels_override_t rc;
        mavlink_msg_rc_channels_override_decode(msg, &rc);
        if (vehicleSysid != 0 && rc.target_system != 0 && rc.target_system != vehicleSysid)
            return false;
        lastMs_ = now;
        seen_ = true;

        const uint16_t raw[CHANNELS] = {
            rc.chan1_raw, rc.chan2_raw, rc.chan3_raw, rc.chan4_raw,
            rc.chan5_raw, rc.chan6_raw, rc.chan7_raw, rc.chan8_raw,
            rc.chan9_raw, rc.chan10_raw, rc.chan11_raw, rc.chan12_raw,
            rc.chan13_raw, rc.chan14_raw, rc.chan15_raw, rc.chan16_raw,
        };
        for (uint8_t ch = 0; ch < CHANNELS; ch++)
        {
            // CH1-8: 0 releases, UINT16_MAX ignores. CH9+ are MAVLink2 extensions, which a
            // MAVLink1 or trimmed frame leaves at 0: there 0 also ignores and UINT16_MAX-1
            // releases. Releasing leaves the channel at the held position.
            const uint16_t value = raw[ch];
            const uint16_t release = ch < 8 ? 0 : UINT16_MAX - 1;
            if (value == release)
                values_[ch] = UNSET;
            else if (value >= US_CHANNEL_VALUE_MIN && value <= US_CHANNEL_VALUE_MAX)
                values_[ch] = usToCrsf(value);
        }
        return true;
    }

    // channels are the handset's, in CRSF units. enableChannel is 0-based, forwardMask has bit n
    // set for channel n (0-based) to be taken from the GCS.
    void apply(uint32_t channels[], size_t channelCount, uint8_t enableChannel, uint16_t forwardMask)
    {
        if (enableChannel >= CHANNELS || channelCount <= enableChannel)
            return;
        forwardMask &= ~(1U << enableChannel);
        const bool allowed = CRSF_to_BIT(channels[enableChannel]);
        const uint8_t count = channelCount < CHANNELS ? channelCount : CHANNELS;
        for (uint8_t ch = 0; ch < count; ch++)
        {
            // Every channel, so one ticked after the handover holds where the handset had it too.
            // If the enable channel is already high on the first frame, that frame is all there is to hold.
            if (!allowed || !holdValid_)
                held_[ch] = channels[ch];
            if (allowed && (forwardMask & (1U << ch)))
                channels[ch] = values_[ch] != UNSET ? values_[ch] : held_[ch];
        }
        holdValid_ = true;
    }

    // For flying with no handset: the GCS's value for each channel in forwardMask it has set, low (1000 us)
    // for every other channel, as that is safe for a switch (arming). False, with channels untouched, when
    // the GCS has sent nothing for timeoutMs or has no value for any channel in the mask: the joystick is
    // gone, so no RC should be sent.
    bool fillAll(uint32_t channels[], size_t channelCount, uint16_t forwardMask, uint32_t now, uint32_t timeoutMs) const
    {
        if (!seen_ || now - lastMs_ > timeoutMs)
            return false;
        bool any = false;
        for (uint8_t ch = 0; ch < CHANNELS; ch++)
            any |= (forwardMask & (1U << ch)) && values_[ch] != UNSET;
        if (!any)
            return false;
        const uint8_t count = channelCount < CHANNELS ? channelCount : CHANNELS;
        for (uint8_t ch = 0; ch < count; ch++)
        {
            const bool fromGcs = (forwardMask & (1U << ch)) && values_[ch] != UNSET;
            channels[ch] = fromGcs ? values_[ch] : CRSF_CHANNEL_VALUE_1000;
        }
        return true;
    }

    // Call while not applying, so turning it on later starts from neither old stick positions nor old GCS values
    void reset() { *this = MavRcOverride(); }

private:
    static constexpr uint16_t UNSET = 0;

    static uint16_t usToCrsf(uint16_t us)
    {
        const uint16_t limited = us < US_CHANNEL_VALUE_STD_MIN ? US_CHANNEL_VALUE_STD_MIN : (us > US_CHANNEL_VALUE_STD_MAX ? US_CHANNEL_VALUE_STD_MAX : us);
        return fmap(limited, US_CHANNEL_VALUE_STD_MIN, US_CHANNEL_VALUE_STD_MAX, CRSF_CHANNEL_VALUE_STD_MIN, CRSF_CHANNEL_VALUE_STD_MAX);
    }

    uint16_t values_[CHANNELS] = {};
    uint16_t held_[CHANNELS] = {};
    bool holdValid_ = false;
    uint32_t lastMs_ = 0;
    bool seen_ = false;
};
