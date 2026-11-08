#include <unity.h>
#include <string.h>
#include <vector>

#include "mavlink_common.h"
#include "MavFrameFilter.h"
#include "MavRcOverride.h"

#define VEHICLE_SYS 7
#define GCS_SYS 255
#define GCS_COMP 190
#define HANDSET_VALUE 500

#define CRSF_1000 191
#define CRSF_1500 992
#define CRSF_2000 1792

// The defaults: CH16 enables, CH1-4 are forwarded
#define ENABLE_CHANNEL 15
#define STICKS_MASK 0x000F

static MavRcOverride rcOverride;
static uint32_t testNow;        // the time overrides arrive at
static uint32_t channels[16];

static void applyOverride(size_t count = 16, uint8_t enableChannel = ENABLE_CHANNEL, uint16_t mask = STICKS_MASK)
{
    rcOverride.apply(channels, count, enableChannel, mask);
}

static void setHandset(uint32_t allowChannel)
{
    for (auto &ch : channels)
        ch = HANDSET_VALUE;
    channels[ENABLE_CHANNEL] = allowChannel;
}

static mavlink_rc_channels_override_t blankOverride()
{
    // Every field says "ignore"
    mavlink_rc_channels_override_t rc = {};
    rc.target_system = VEHICLE_SYS;
    rc.target_component = 1;
    rc.chan1_raw = rc.chan2_raw = rc.chan3_raw = rc.chan4_raw = UINT16_MAX;
    rc.chan5_raw = rc.chan6_raw = rc.chan7_raw = rc.chan8_raw = UINT16_MAX;
    return rc;
}

static bool sendOverride(const mavlink_rc_channels_override_t &rc)
{
    mavlink_message_t msg;
    mavlink_msg_rc_channels_override_encode(GCS_SYS, GCS_COMP, &msg, &rc);
    return rcOverride.handleMessage(&msg, VEHICLE_SYS, testNow);
}

static std::vector<uint8_t> packed(const mavlink_message_t &msg)
{
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    const uint16_t len = mavlink_msg_to_send_buffer(buf, &msg);
    return std::vector<uint8_t>(buf, buf + len);
}

static std::vector<uint8_t> overrideFrame(uint16_t ch1)
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = ch1;
    mavlink_message_t msg;
    mavlink_msg_rc_channels_override_encode(GCS_SYS, GCS_COMP, &msg, &rc);
    return packed(msg);
}

static std::vector<uint8_t> heartbeatFrame()
{
    mavlink_message_t msg;
    mavlink_msg_heartbeat_pack(GCS_SYS, GCS_COMP, &msg, MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_ACTIVE);
    return packed(msg);
}

static std::vector<uint8_t> finishFrame(std::vector<uint8_t> f, uint8_t crcExtra)
{
    uint16_t crc = crc_calculate(f.data() + 1, f.size() - 1);
    crc_accumulate(crcExtra, &crc);
    f.push_back(crc & 0xFF);
    f.push_back(crc >> 8);
    return f;
}

static std::vector<uint8_t> uplink;
static int otherFrames;
static MavFrameFilter filter;

static void captureUplink(const uint8_t *data, uint16_t len)
{
    uplink.insert(uplink.end(), data, data + len);
}

static bool onFrame(const mavlink_message_t *msg, uint32_t now)
{
    if (rcOverride.handleMessage(msg, VEHICLE_SYS, testNow))
        return true;
    otherFrames++;
    return false;
}

static void feed(const std::vector<uint8_t> &bytes, uint32_t now = 0)
{
    filter.feed(bytes.data(), bytes.size(), now, onFrame, captureUplink);
}

static std::vector<uint8_t> joined(std::vector<std::vector<uint8_t>> parts)
{
    std::vector<uint8_t> out;
    for (auto &part : parts)
        out.insert(out.end(), part.begin(), part.end());
    return out;
}

void setUp()
{
    rcOverride = MavRcOverride();
    testNow = 1000;
    filter = MavFrameFilter();
    uplink.clear();
    otherFrames = 0;
    setHandset(CRSF_2000);
}

void tearDown() {}

void test_allowed_override_replaces_only_the_channels_it_sets()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 1000;
    rc.chan2_raw = 1500;
    rc.chan3_raw = 2000;
    TEST_ASSERT_TRUE(sendOverride(rc));
    applyOverride();
    TEST_ASSERT_EQUAL(CRSF_1000, channels[0]);
    TEST_ASSERT_EQUAL(CRSF_1500, channels[1]);
    TEST_ASSERT_EQUAL(CRSF_2000, channels[2]);
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[3]);
}
void test_blocked_unless_allow_channel_is_high()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 2000;
    // Blocked or not, the message is consumed
    TEST_ASSERT_TRUE(sendOverride(rc));

    setHandset(CRSF_1000);
    applyOverride();
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[0]);

    // A three position switch at its centre is not an allow
    setHandset(CRSF_1500);
    applyOverride();
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[0]);

    // No CH16 from this handset (e.g. PPM with fewer channels)
    setHandset(CRSF_2000);
    applyOverride(15);
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[0]);

    applyOverride();
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);
}

void test_only_ch1_to_4_are_replaced()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = rc.chan2_raw = rc.chan3_raw = rc.chan4_raw = 2000;
    rc.chan5_raw = rc.chan6_raw = rc.chan7_raw = rc.chan8_raw = 1200;
    rc.chan9_raw = rc.chan10_raw = rc.chan11_raw = rc.chan12_raw = 1200;
    rc.chan13_raw = rc.chan14_raw = rc.chan15_raw = rc.chan16_raw = 1200;
    TEST_ASSERT_TRUE(sendOverride(rc));

    // Each with its own value, to catch channels being mixed up as well as replaced
    uint32_t handset[16];
    for (uint8_t ch = 0; ch < 16; ch++)
        channels[ch] = handset[ch] = 300 + ch * 10;
    channels[ENABLE_CHANNEL] = handset[ENABLE_CHANNEL] = CRSF_2000;

    applyOverride();
    for (uint8_t ch = 0; ch < 4; ch++)
        TEST_ASSERT_EQUAL(CRSF_2000, channels[ch]);
    for (uint8_t ch = 4; ch < 16; ch++)
        TEST_ASSERT_EQUAL(handset[ch], channels[ch]);
}

void test_switching_over_never_zeroes_a_channel()
{
    // Nothing from the GCS yet
    applyOverride();
    for (uint8_t ch = 0; ch < 15; ch++)
        TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[ch]);

    // All zeros, as a frame trimmed down to nothing reads: every channel stays where it was
    mavlink_rc_channels_override_t zeros = {};
    zeros.target_system = VEHICLE_SYS;
    TEST_ASSERT_TRUE(sendOverride(zeros));
    applyOverride();
    for (uint8_t ch = 0; ch < 15; ch++)
        TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[ch]);

    // Allowed, blocked and allowed again, with a partial override
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 2000;
    sendOverride(rc);
    for (uint32_t allow : {CRSF_2000, CRSF_1000, CRSF_2000})
    {
        setHandset(allow);
        applyOverride();
        TEST_ASSERT_EQUAL(allow == CRSF_2000 ? CRSF_2000 : HANDSET_VALUE, channels[0]);
        for (uint8_t ch = 1; ch < 15; ch++)
            TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[ch]);
    }
}
void test_nothing_applied_before_the_first_override()
{
    applyOverride();
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[0]);
}

static void moveSticks(uint32_t value, uint32_t allowChannel)
{
    for (uint8_t ch = 0; ch < 4; ch++)
        channels[ch] = value;
    channels[ENABLE_CHANNEL] = allowChannel;
}

void test_sticks_have_no_effect_while_allow_channel_is_high()
{
    moveSticks(400, CRSF_1000);
    applyOverride();
    for (uint8_t ch = 0; ch < 4; ch++)
        TEST_ASSERT_EQUAL(400, channels[ch]);

    // With nothing at all from the GCS, they stay where they were
    moveSticks(1700, CRSF_2000);
    applyOverride();
    for (uint8_t ch = 0; ch < 4; ch++)
        TEST_ASSERT_EQUAL(400, channels[ch]);

    // And follow the handset again as soon as the switch goes back
    moveSticks(1700, CRSF_1000);
    applyOverride();
    for (uint8_t ch = 0; ch < 4; ch++)
        TEST_ASSERT_EQUAL(1700, channels[ch]);

    // A new hold starts from there
    moveSticks(600, CRSF_2000);
    applyOverride();
    for (uint8_t ch = 0; ch < 4; ch++)
        TEST_ASSERT_EQUAL(1700, channels[ch]);
}

void test_held_channels_are_not_touched_beyond_the_sticks()
{
    moveSticks(400, CRSF_1000);
    applyOverride();

    moveSticks(1700, CRSF_2000);
    channels[4] = 1100;
    channels[5] = 1200;
    applyOverride();
    TEST_ASSERT_EQUAL(1100, channels[4]);
    TEST_ASSERT_EQUAL(1200, channels[5]);
}

void test_gcs_sets_only_its_channels_over_the_hold()
{
    moveSticks(400, CRSF_1000);
    applyOverride();

    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 2000;
    sendOverride(rc);
    moveSticks(1700, CRSF_2000);
    applyOverride();
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);
    TEST_ASSERT_EQUAL(400, channels[1]);

    // Given up by the GCS, back to the hold and not to the moving stick
    rc.chan1_raw = 0;
    sendOverride(rc);
    moveSticks(1700, CRSF_2000);
    applyOverride();
    TEST_ASSERT_EQUAL(400, channels[0]);
}

void test_allow_channel_high_from_the_first_frame_holds_that_frame()
{
    moveSticks(400, CRSF_2000);
    applyOverride();
    TEST_ASSERT_EQUAL(400, channels[0]);

    moveSticks(1700, CRSF_2000);
    applyOverride();
    TEST_ASSERT_EQUAL(400, channels[0]);
}

void test_reset_drops_old_stick_positions()
{
    moveSticks(400, CRSF_1000);
    applyOverride();

    rcOverride.reset();
    moveSticks(1700, CRSF_2000);
    applyOverride();
    TEST_ASSERT_EQUAL(1700, channels[0]);
}

void test_reset_drops_old_gcs_values()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 2000;
    sendOverride(rc);
    applyOverride();
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);

    rcOverride.reset();
    setHandset(CRSF_2000);
    applyOverride();
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[0]);
}

void test_values_hold_until_changed_with_no_feed()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 2000;
    sendOverride(rc);
    applyOverride();
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);

    // Still there after the handset blocks and allows again, with nothing new from the GCS
    setHandset(CRSF_1000);
    applyOverride();
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[0]);
    setHandset(CRSF_2000);
    applyOverride();
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);

    // Until the GCS releases the channel
    rc.chan1_raw = 0;
    sendOverride(rc);
    setHandset(CRSF_2000);
    applyOverride();
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[0]);
}

void test_field_values_with_special_meanings()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 2000;
    rc.chan2_raw = 2000;
    sendOverride(rc);

    rc.chan1_raw = UINT16_MAX;  // ignore, keeps its value
    rc.chan2_raw = 0;           // release
    sendOverride(rc);

    applyOverride();
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[1]);
}
void test_forward_mask_picks_the_channels()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 2000;
    rc.chan2_raw = 2000;
    rc.chan6_raw = 1000;
    rc.chan12_raw = 1500;
    rc.chan16_raw = 1000;
    sendOverride(rc);

    // CH5 enables, CH1, CH6 and CH12 are forwarded
    setHandset(CRSF_1000);
    channels[4] = CRSF_2000;
    applyOverride(16, 4, (1 << 0) | (1 << 5) | (1 << 11));
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[1]);
    TEST_ASSERT_EQUAL(CRSF_1000, channels[5]);
    TEST_ASSERT_EQUAL(CRSF_1500, channels[11]);
    // Not in the mask, and CH16 is an ordinary channel here
    TEST_ASSERT_EQUAL(CRSF_1000, channels[15]);
}

void test_enable_channel_is_never_forwarded()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 2000;
    rc.chan5_raw = 1000;
    sendOverride(rc);

    setHandset(CRSF_1000);
    channels[4] = CRSF_2000;
    applyOverride(16, 4, 0xFFFF);
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);
    TEST_ASSERT_EQUAL(CRSF_2000, channels[4]);

    // Still enabled next frame, so the GCS didn't turn it off either
    applyOverride(16, 4, 0xFFFF);
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);
}

void test_enable_channel_out_of_range_does_nothing()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 2000;
    sendOverride(rc);
    applyOverride(16, 16, STICKS_MASK);
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[0]);
}

void test_extension_channel_values_with_special_meanings()
{
    const uint16_t ch9 = 1 << 8;
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan9_raw = 2000;
    sendOverride(rc);
    applyOverride(16, ENABLE_CHANNEL, ch9);
    TEST_ASSERT_EQUAL(CRSF_2000, channels[8]);

    // 0, as a MAVLink1 or trimmed frame reads, and UINT16_MAX both ignore
    for (uint16_t ignore : {(uint16_t)0, (uint16_t)UINT16_MAX})
    {
        rc.chan9_raw = ignore;
        sendOverride(rc);
        setHandset(CRSF_2000);
        applyOverride(16, ENABLE_CHANNEL, ch9);
        TEST_ASSERT_EQUAL(CRSF_2000, channels[8]);
    }

    // UINT16_MAX-1 releases, back to the hold
    rc.chan9_raw = UINT16_MAX - 1;
    sendOverride(rc);
    setHandset(CRSF_2000);
    applyOverride(16, ENABLE_CHANNEL, ch9);
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[8]);
}

void test_channel_ticked_after_the_handover_holds_the_handset_position()
{
    // Handover with only CH1 ticked, then CH2 ticked too while the enable channel stays high:
    // CH2 holds where the handset had it at the handover, never 0
    moveSticks(400, CRSF_1000);
    applyOverride(16, ENABLE_CHANNEL, 1 << 0);
    moveSticks(400, CRSF_2000);
    applyOverride(16, ENABLE_CHANNEL, 1 << 0);

    moveSticks(1700, CRSF_2000);
    applyOverride(16, ENABLE_CHANNEL, (1 << 0) | (1 << 1));
    TEST_ASSERT_EQUAL(400, channels[0]);
    TEST_ASSERT_EQUAL(400, channels[1]);
}

// ---- no handset: the mask's channels from the GCS, every other channel low ---------------------

#define GCS_ONLY_TIMEOUT_MS 1000
#define LOW_1000US 191

void test_no_handset_sends_nothing_before_the_first_override()
{
    TEST_ASSERT_FALSE(rcOverride.fillAll(channels, 16, STICKS_MASK, testNow, GCS_ONLY_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[0]);     // untouched
}

void test_no_handset_takes_the_ticked_channels_and_sends_the_rest_low()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 1000;
    rc.chan3_raw = 2000;
    rc.chan5_raw = 2000;                               // sent but not ticked: stays low
    rc.chan9_raw = 1250;                               // a MAVLink 2 extension field
    sendOverride(rc);
    const uint16_t mask = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 8);
    TEST_ASSERT_TRUE(rcOverride.fillAll(channels, 16, mask, testNow + 20, GCS_ONLY_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(CRSF_1000, channels[0]);
    TEST_ASSERT_EQUAL(LOW_1000US, channels[1]);        // ticked, never set by the GCS
    TEST_ASSERT_EQUAL(CRSF_2000, channels[2]);
    TEST_ASSERT_EQUAL(LOW_1000US, channels[4]);
    TEST_ASSERT_EQUAL(591, channels[8]);               // 1250 us
    TEST_ASSERT_EQUAL(LOW_1000US, channels[15]);
}

void test_no_handset_stops_when_the_gcs_goes_quiet()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 2000;
    sendOverride(rc);
    TEST_ASSERT_TRUE(rcOverride.fillAll(channels, 16, STICKS_MASK, testNow + GCS_ONLY_TIMEOUT_MS, GCS_ONLY_TIMEOUT_MS));
    setHandset(CRSF_1000);
    TEST_ASSERT_FALSE(rcOverride.fillAll(channels, 16, STICKS_MASK, testNow + GCS_ONLY_TIMEOUT_MS + 1, GCS_ONLY_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[0]);     // untouched

    // Back as soon as the GCS sends again
    testNow += 5000;
    sendOverride(rc);
    TEST_ASSERT_TRUE(rcOverride.fillAll(channels, 16, STICKS_MASK, testNow, GCS_ONLY_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);
}

void test_no_handset_stops_when_the_gcs_releases_the_ticked_channels()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 2000;
    rc.chan5_raw = 2000;                               // not ticked: does not keep RC going
    sendOverride(rc);
    rc.chan1_raw = 0;                                  // release
    sendOverride(rc);
    TEST_ASSERT_FALSE(rcOverride.fillAll(channels, 16, STICKS_MASK, testNow, GCS_ONLY_TIMEOUT_MS));
}

void test_pulse_widths_are_limited()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 900;              // beyond the OTA range, limited
    rc.chan2_raw = 2100;
    rc.chan3_raw = UINT16_MAX - 1;   // not a pulse width
    rc.chan4_raw = 100;
    sendOverride(rc);
    applyOverride();
    TEST_ASSERT_EQUAL(CRSF_CHANNEL_VALUE_STD_MIN, channels[0]);
    TEST_ASSERT_EQUAL(CRSF_CHANNEL_VALUE_STD_MAX, channels[1]);
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[2]);
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[3]);
}

void test_addressing()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 2000;

    // Another vehicle's: left alone, to travel on
    rc.target_system = VEHICLE_SYS + 1;
    TEST_ASSERT_FALSE(sendOverride(rc));
    applyOverride();
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[0]);

    // Broadcast
    rc.target_system = 0;
    TEST_ASSERT_TRUE(sendOverride(rc));

    // The GCS may have this TX selected rather than the autopilot
    rc.target_system = VEHICLE_SYS;
    rc.target_component = 241;
    TEST_ASSERT_TRUE(sendOverride(rc));

    // Vehicle not seen yet
    mavlink_message_t msg;
    rc.target_system = VEHICLE_SYS + 1;
    mavlink_msg_rc_channels_override_encode(GCS_SYS, GCS_COMP, &msg, &rc);
    TEST_ASSERT_TRUE(rcOverride.handleMessage(&msg, 0, testNow));

    mavlink_msg_heartbeat_pack(GCS_SYS, GCS_COMP, &msg, MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_ACTIVE);
    TEST_ASSERT_FALSE(rcOverride.handleMessage(&msg, VEHICLE_SYS, testNow));
}

void test_filter_removes_overrides_and_keeps_the_rest_in_order()
{
    const std::vector<uint8_t> hb = heartbeatFrame();
    const std::vector<uint8_t> first = joined({hb, overrideFrame(2000), hb});
    feed(first, 1000);
    TEST_ASSERT_TRUE(uplink == joined({hb, hb}));
    TEST_ASSERT_EQUAL(2, otherFrames);
    applyOverride();
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);
}

void test_filter_handles_frames_split_across_reads()
{
    const std::vector<uint8_t> hb = heartbeatFrame();
    const std::vector<uint8_t> stream = joined({hb, overrideFrame(1000), hb, overrideFrame(2000)});
    for (uint8_t b : stream)
        feed({b}, 1000);
    TEST_ASSERT_TRUE(uplink == joined({hb, hb}));
    applyOverride();
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);
}

void test_filter_removes_mavlink1_overrides()
{
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.chan1_raw = 2000;
    mavlink_status_t status = {};
    status.flags = MAVLINK_STATUS_FLAG_OUT_MAVLINK1;
    mavlink_message_t msg;
    mavlink_msg_rc_channels_override_encode_status(GCS_SYS, GCS_COMP, &status, &msg, &rc);
    const std::vector<uint8_t> frame = packed(msg);
    TEST_ASSERT_EQUAL(MAVLINK_STX_MAVLINK1, frame[0]);
    feed(frame, 1000);
    TEST_ASSERT_EQUAL(0, uplink.size());
    applyOverride();
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);
}

void test_filter_passes_everything_else_unchanged()
{
    // An override with a bad CRC isn't acted on
    std::vector<uint8_t> corrupt = overrideFrame(2000);
    corrupt[12] ^= 0x55;
    feed(corrupt, 1000);
    TEST_ASSERT_TRUE(uplink == corrupt);
    applyOverride();
    TEST_ASSERT_EQUAL(HANDSET_VALUE, channels[0]);
    uplink.clear();

    // Another vehicle's override
    mavlink_rc_channels_override_t rc = blankOverride();
    rc.target_system = VEHICLE_SYS + 1;
    mavlink_message_t msg;
    mavlink_msg_rc_channels_override_encode(GCS_SYS, GCS_COMP, &msg, &rc);
    const std::vector<uint8_t> forOther = packed(msg);
    feed(forOther);
    TEST_ASSERT_TRUE(uplink == forOther);
    uplink.clear();

    // A message this dialect doesn't know, valid for its own CRC extra
    std::vector<uint8_t> unknown = {MAVLINK_STX, 5, 0, 0, 9, 1, 241, 60045 & 0xFF, (60045 >> 8) & 0xFF, 0, 1, 2, 3, 4, 5};
    unknown = finishFrame(unknown, 14);
    feed(unknown);
    TEST_ASSERT_TRUE(uplink == unknown);
    uplink.clear();

    // Signed: the signature goes along
    const std::vector<uint8_t> hb = heartbeatFrame();
    std::vector<uint8_t> sig(hb.begin(), hb.end() - 2);
    sig[2] |= MAVLINK_IFLAG_SIGNED;
    sig = finishFrame(sig, MAVLINK_MSG_ID_HEARTBEAT_CRC);
    for (uint8_t i = 0; i < MAVLINK_SIGNATURE_BLOCK_LEN; i++)
        sig.push_back(0xA0 + i);
    feed(sig);
    TEST_ASSERT_TRUE(uplink == sig);
    uplink.clear();

    // Not MAVLink at all
    const std::vector<uint8_t> garbage = {0x01, 0x02, 0xC8, 0x18, 0x16};
    feed(garbage);
    TEST_ASSERT_TRUE(uplink == garbage);
}

void test_filter_lets_go_of_a_frame_cut_short()
{
    const std::vector<uint8_t> hb = heartbeatFrame();
    const std::vector<uint8_t> cut(hb.begin(), hb.begin() + 8);
    feed(cut, 1000);
    TEST_ASSERT_EQUAL(0, uplink.size());

    filter.update(1500, 500, captureUplink);
    TEST_ASSERT_EQUAL(0, uplink.size());

    filter.update(1501, 500, captureUplink);
    TEST_ASSERT_TRUE(uplink == cut);

    // The next frames are parsed from their start, and an override among them is still removed
    uplink.clear();
    feed(joined({overrideFrame(2000), hb}), 2000);
    TEST_ASSERT_TRUE(uplink == hb);
    applyOverride();
    TEST_ASSERT_EQUAL(CRSF_2000, channels[0]);
}

void test_filter_handles_the_longest_frame()
{
    std::vector<uint8_t> longest = {MAVLINK_STX, 255, MAVLINK_IFLAG_SIGNED, 0, 9, 1, 241, 60045 & 0xFF, (60045 >> 8) & 0xFF, 0};
    longest.insert(longest.end(), 255, 0x5A);
    longest = finishFrame(longest, 14);
    longest.insert(longest.end(), MAVLINK_SIGNATURE_BLOCK_LEN, 0xA5);
    TEST_ASSERT_EQUAL(MAVLINK_MAX_PACKET_LEN, longest.size());
    feed(longest);
    TEST_ASSERT_TRUE(uplink == longest);
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_allowed_override_replaces_only_the_channels_it_sets);
    RUN_TEST(test_blocked_unless_allow_channel_is_high);
    RUN_TEST(test_only_ch1_to_4_are_replaced);
    RUN_TEST(test_switching_over_never_zeroes_a_channel);
    RUN_TEST(test_nothing_applied_before_the_first_override);
    RUN_TEST(test_sticks_have_no_effect_while_allow_channel_is_high);
    RUN_TEST(test_held_channels_are_not_touched_beyond_the_sticks);
    RUN_TEST(test_gcs_sets_only_its_channels_over_the_hold);
    RUN_TEST(test_allow_channel_high_from_the_first_frame_holds_that_frame);
    RUN_TEST(test_reset_drops_old_stick_positions);
    RUN_TEST(test_reset_drops_old_gcs_values);
    RUN_TEST(test_values_hold_until_changed_with_no_feed);
    RUN_TEST(test_field_values_with_special_meanings);
    RUN_TEST(test_forward_mask_picks_the_channels);
    RUN_TEST(test_enable_channel_is_never_forwarded);
    RUN_TEST(test_enable_channel_out_of_range_does_nothing);
    RUN_TEST(test_extension_channel_values_with_special_meanings);
    RUN_TEST(test_channel_ticked_after_the_handover_holds_the_handset_position);
    RUN_TEST(test_no_handset_sends_nothing_before_the_first_override);
    RUN_TEST(test_no_handset_takes_the_ticked_channels_and_sends_the_rest_low);
    RUN_TEST(test_no_handset_stops_when_the_gcs_goes_quiet);
    RUN_TEST(test_no_handset_stops_when_the_gcs_releases_the_ticked_channels);
    RUN_TEST(test_pulse_widths_are_limited);
    RUN_TEST(test_addressing);
    RUN_TEST(test_filter_removes_overrides_and_keeps_the_rest_in_order);
    RUN_TEST(test_filter_handles_frames_split_across_reads);
    RUN_TEST(test_filter_removes_mavlink1_overrides);
    RUN_TEST(test_filter_passes_everything_else_unchanged);
    RUN_TEST(test_filter_lets_go_of_a_frame_cut_short);
    RUN_TEST(test_filter_handles_the_longest_frame);
    UNITY_END();
    return 0;
}
