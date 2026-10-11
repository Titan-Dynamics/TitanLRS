#include <unity.h>
#include <string.h>
#include <vector>

#include "MavParamServer.h"
#include "common/mavlink.h"
#include "MavFrame.h"


#define COMPID 241
#define GCS_SYS 255
#define GCS_COMP 190

static std::vector<mavlink_message_t> sent;
static bool sendAccepts;
static mavlink_message_t parseMsg;
static mavlink_status_t parseStatus;

static bool captureFrame(const uint8_t *frame, uint16_t len)
{
    if (!sendAccepts)
        return false;
    for (uint16_t i = 0; i < len; i++)
    {
        mavlink_message_t msg;
        mavlink_status_t status;
        if (mavlink_frame_char_buffer(&parseMsg, &parseStatus, frame[i], &msg, &status) == MAVLINK_FRAMING_OK)
            sent.push_back(msg);
    }
    return true;
}

static float valueA, valueB, valueRo;
static bool lockedA;
static bool availableC;

static float getA() { return valueA; }
static float getB() { return valueB; }
static float getRo() { return valueRo; }
static const char *setA(float v)
{
    if (v > 100)
        return "out of range";
    valueA = v;
    valueB = v * 2; // B follows A
    return nullptr;
}
static const char *setB(float v)
{
    valueB = v;
    return nullptr;
}
static const char *lockA() { return lockedA ? "locked in test" : nullptr; }
static bool hasC() { return availableC; }
static uint8_t fallback() { return 1; }

static const MavParam table[] = {
    {"TST_A", nullptr, getA, setA, lockA},
    {"TST_C", hasC, getB, setB, nullptr},
    {"TST_B", nullptr, getB, setB, nullptr},
    {"TST_RO", nullptr, getRo, nullptr, nullptr},
};

static MavParamServer server;

static uint32_t heartbeatInterval;

static MavParamServer::Config config(uint32_t wait, uint32_t delay, uint8_t perTick)
{
    MavParamServer::Config c = {};
    c.heartbeatIntervalMs = heartbeatInterval;
    c.compid = COMPID;
    c.fallbackSysid = fallback;
    c.heartbeatWaitMs = wait;
    c.replyDelayMs = delay;
    c.maxFramesPerTick = perTick;
    c.swVersion = 0x01020300;
    return c;
}

static void start(uint32_t wait = 0, uint32_t delay = 0, uint8_t perTick = 32)
{
    sent.clear();
    sendAccepts = true;
    memset(&parseMsg, 0, sizeof(parseMsg));
    memset(&parseStatus, 0, sizeof(parseStatus));
    server = MavParamServer();
    server.begin(table, sizeof(table) / sizeof(table[0]), config(wait, delay, perTick), captureFrame, 0);
}

static void vehicleHeartbeat(uint8_t sysid, uint8_t compid, uint8_t type, uint8_t autopilot)
{
    mavlink_message_t msg;
    mavlink_msg_heartbeat_pack(sysid, compid, &msg, type, autopilot, 0, 0, MAV_STATE_ACTIVE);
    server.handleVehicleMessage(&msg);
}

static void gcs(mavlink_message_t &msg, uint32_t now = 0) { server.handleGcsMessage(&msg, now); }

static void requestList(uint8_t ts, uint8_t tc)
{
    mavlink_message_t msg;
    mavlink_msg_param_request_list_pack(GCS_SYS, GCS_COMP, &msg, ts, tc);
    gcs(msg);
}

static void paramSet(uint8_t ts, uint8_t tc, const char *id, float value, uint32_t now = 0)
{
    mavlink_message_t msg;
    mavlink_msg_param_set_pack(GCS_SYS, GCS_COMP, &msg, ts, tc, id, value, MAV_PARAM_TYPE_REAL32);
    gcs(msg, now);
}

static std::vector<mavlink_param_value_t> paramValues()
{
    std::vector<mavlink_param_value_t> out;
    for (auto &m : sent)
    {
        if (m.msgid == MAVLINK_MSG_ID_PARAM_VALUE)
        {
            mavlink_param_value_t v;
            mavlink_msg_param_value_decode(&m, &v);
            out.push_back(v);
        }
    }
    return out;
}

static int countOf(uint32_t msgid)
{
    int n = 0;
    for (auto &m : sent)
        n += m.msgid == msgid;
    return n;
}

void setUp()
{
    heartbeatInterval = 0;
    valueA = 5;
    valueB = 10;
    valueRo = 7;
    lockedA = false;
    availableC = false;
}

void tearDown() {}


void test_unavailable_params_are_not_listed()
{
    start();
    TEST_ASSERT_EQUAL(3, server.paramCount());
    TEST_ASSERT_EQUAL_STRING("TST_A", server.param(0)->id);
    TEST_ASSERT_EQUAL_STRING("TST_B", server.param(1)->id);
    TEST_ASSERT_EQUAL_STRING("TST_RO", server.param(2)->id);
    availableC = true;
    start();
    TEST_ASSERT_EQUAL(4, server.paramCount());
}

void test_heartbeat_waits_for_vehicle_then_uses_fallback()
{
    start(5000);
    server.tick(100);
    server.tick(4999);
    TEST_ASSERT_EQUAL(0, countOf(MAVLINK_MSG_ID_HEARTBEAT));
    server.tick(5000);
    TEST_ASSERT_EQUAL(1, countOf(MAVLINK_MSG_ID_HEARTBEAT));
    TEST_ASSERT_EQUAL(1, sent.back().sysid);
    server.tick(5500);
    TEST_ASSERT_EQUAL(1, countOf(MAVLINK_MSG_ID_HEARTBEAT));
    server.tick(6000);
    TEST_ASSERT_EQUAL(2, countOf(MAVLINK_MSG_ID_HEARTBEAT));
}

void test_heartbeat_starts_with_vehicle_and_identifies_us()
{
    start(5000);
    vehicleHeartbeat(7, 1, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_ARDUPILOTMEGA);
    server.tick(10);
    TEST_ASSERT_EQUAL(1, countOf(MAVLINK_MSG_ID_HEARTBEAT));
    mavlink_heartbeat_t hb;
    mavlink_msg_heartbeat_decode(&sent.back(), &hb);
    TEST_ASSERT_EQUAL(7, sent.back().sysid);
    TEST_ASSERT_EQUAL(COMPID, sent.back().compid);
    TEST_ASSERT_EQUAL(MAV_AUTOPILOT_INVALID, hb.autopilot);
    TEST_ASSERT_EQUAL(MAV_TYPE_GENERIC, hb.type);
    TEST_ASSERT_EQUAL_HEX32(TLRS_MAV_HEARTBEAT_MARKER, hb.custom_mode);
}

void test_sysid_latched_to_first_autopilot()
{
    start();
    vehicleHeartbeat(9, 154, MAV_TYPE_GIMBAL, MAV_AUTOPILOT_INVALID); // gimbal: ignored
    vehicleHeartbeat(255, 190, MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID);  // GCS: ignored
    TEST_ASSERT_EQUAL(0, server.vehicleSysid());
    vehicleHeartbeat(3, 1, MAV_TYPE_FIXED_WING, MAV_AUTOPILOT_ARDUPILOTMEGA);
    vehicleHeartbeat(4, 1, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_PX4); // second vehicle: ignored
    TEST_ASSERT_EQUAL(3, server.vehicleSysid());
    TEST_ASSERT_EQUAL(3, server.sysid());
}

void test_request_list_paced_and_complete()
{
    start(0, 0, 2);
    vehicleHeartbeat(1, 1, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_ARDUPILOTMEGA);
    server.tick(0); // heartbeat
    sent.clear();
    requestList(1, COMPID);
    server.tick(10);
    TEST_ASSERT_EQUAL(2, countOf(MAVLINK_MSG_ID_PARAM_VALUE));
    server.tick(20);
    auto values = paramValues();
    TEST_ASSERT_EQUAL(3, values.size());
    for (uint16_t i = 0; i < values.size(); i++)
    {
        TEST_ASSERT_EQUAL(i, values[i].param_index);
        TEST_ASSERT_EQUAL(3, values[i].param_count);
        TEST_ASSERT_EQUAL(MAV_PARAM_TYPE_REAL32, values[i].param_type);
    }
    TEST_ASSERT_EQUAL_FLOAT(5, values[0].param_value);
    TEST_ASSERT_FALSE(server.hasPendingOutput());
}

void test_request_list_addressing()
{
    start();
    requestList(1, 0); // all components (QGC)
    server.tick(0);
    TEST_ASSERT_EQUAL(3, countOf(MAVLINK_MSG_ID_PARAM_VALUE));
    sent.clear();
    requestList(0, 0); // broadcast
    server.tick(1);
    TEST_ASSERT_EQUAL(3, countOf(MAVLINK_MSG_ID_PARAM_VALUE));
    sent.clear();
    requestList(1, 1);      // the autopilot
    requestList(2, COMPID); // another vehicle
    server.tick(2);
    TEST_ASSERT_EQUAL(0, countOf(MAVLINK_MSG_ID_PARAM_VALUE));
}

void test_request_read_by_name_and_index()
{
    start();
    mavlink_message_t msg;
    mavlink_msg_param_request_read_pack(GCS_SYS, GCS_COMP, &msg, 1, COMPID, "TST_RO", -1);
    gcs(msg);
    mavlink_msg_param_request_read_pack(GCS_SYS, GCS_COMP, &msg, 1, COMPID, "", 1);
    gcs(msg);
    mavlink_msg_param_request_read_pack(GCS_SYS, GCS_COMP, &msg, 1, COMPID, "", 3); // out of range
    gcs(msg);
    mavlink_msg_param_request_read_pack(GCS_SYS, GCS_COMP, &msg, 1, COMPID, "NOPE", -1);
    gcs(msg);
    server.tick(0);
    auto values = paramValues();
    TEST_ASSERT_EQUAL(2, values.size());
    TEST_ASSERT_EQUAL(1, values[0].param_index);
    TEST_ASSERT_EQUAL(2, values[1].param_index);
    TEST_ASSERT_EQUAL_STRING_LEN("TST_RO", values[1].param_id, 6);
}

void test_set_replies_after_delay_with_changed_values()
{
    start(0, 100);
    paramSet(1, COMPID, "TST_A", 20, 1000);
    TEST_ASSERT_EQUAL_FLOAT(20, valueA);
    server.tick(1050);
    TEST_ASSERT_EQUAL(0, countOf(MAVLINK_MSG_ID_PARAM_VALUE));
    TEST_ASSERT_TRUE(server.hasPendingOutput());
    server.tick(1100);
    auto values = paramValues();
    TEST_ASSERT_EQUAL(2, values.size()); // A, and B which changed with it
    TEST_ASSERT_EQUAL_FLOAT(20, values[0].param_value);
    TEST_ASSERT_EQUAL_FLOAT(40, values[1].param_value);
    TEST_ASSERT_EQUAL(0, countOf(MAVLINK_MSG_ID_STATUSTEXT));
}

void test_set_must_be_addressed_exactly()
{
    start();
    paramSet(1, 0, "TST_A", 20);
    paramSet(0, COMPID, "TST_A", 21);
    paramSet(1, 1, "TST_A", 22);
    server.tick(0);
    TEST_ASSERT_EQUAL_FLOAT(5, valueA);
    TEST_ASSERT_EQUAL(0, countOf(MAVLINK_MSG_ID_PARAM_VALUE));
}

static void assertRejected(const char *id, float current, const char *text)
{
    auto values = paramValues();
    TEST_ASSERT_EQUAL(1, values.size());
    TEST_ASSERT_EQUAL_STRING_LEN(id, values[0].param_id, strlen(id));
    TEST_ASSERT_EQUAL_FLOAT(current, values[0].param_value);
    TEST_ASSERT_EQUAL(1, countOf(MAVLINK_MSG_ID_STATUSTEXT));
    for (auto &m : sent)
    {
        if (m.msgid == MAVLINK_MSG_ID_STATUSTEXT)
        {
            mavlink_statustext_t st;
            mavlink_msg_statustext_decode(&m, &st);
            char buf[51] = {};
            memcpy(buf, st.text, 50);
            TEST_ASSERT_EQUAL_STRING(text, buf);
            TEST_ASSERT_EQUAL(MAV_SEVERITY_NOTICE, st.severity);
        }
    }
}

void test_set_locked_is_refused_with_reason()
{
    start(0, 100);
    lockedA = true;
    paramSet(1, COMPID, "TST_A", 20);
    server.tick(0); // refusals are answered at once
    TEST_ASSERT_EQUAL_FLOAT(5, valueA);
    assertRejected("TST_A", 5, "TST_A: locked in test");
}

void test_set_refused_by_setter()
{
    start();
    paramSet(1, COMPID, "TST_A", 500);
    server.tick(0);
    TEST_ASSERT_EQUAL_FLOAT(5, valueA);
    assertRejected("TST_A", 5, "TST_A: out of range");
}

void test_set_read_only()
{
    start();
    paramSet(1, COMPID, "TST_RO", 1);
    server.tick(0);
    assertRejected("TST_RO", 7, "TST_RO: read-only");
}

void test_ftp_is_refused()
{
    start();
    uint8_t payload[251] = {};
    payload[0] = 0x34; // seq 0x1234
    payload[1] = 0x12;
    payload[3] = 4; // OpenFileRO
    mavlink_message_t msg;
    mavlink_msg_file_transfer_protocol_pack(GCS_SYS, GCS_COMP, &msg, 0, 1, COMPID, payload);
    gcs(msg);
    payload[3] = 2; // ResetSessions
    mavlink_msg_file_transfer_protocol_pack(GCS_SYS, GCS_COMP, &msg, 0, 1, 1, payload); // for the autopilot
    gcs(msg);
    server.tick(0);
    TEST_ASSERT_EQUAL(1, countOf(MAVLINK_MSG_ID_FILE_TRANSFER_PROTOCOL));
    mavlink_file_transfer_protocol_t reply;
    mavlink_msg_file_transfer_protocol_decode(&sent[0], &reply);
    TEST_ASSERT_EQUAL(GCS_SYS, reply.target_system);
    TEST_ASSERT_EQUAL(GCS_COMP, reply.target_component);
    TEST_ASSERT_EQUAL(0x35, reply.payload[0]);
    TEST_ASSERT_EQUAL(0x12, reply.payload[1]);
    TEST_ASSERT_EQUAL(129, reply.payload[3]); // NAK
    TEST_ASSERT_EQUAL(1, reply.payload[4]);
    TEST_ASSERT_EQUAL(4, reply.payload[5]);
    TEST_ASSERT_EQUAL(10, reply.payload[12]); // FileNotFound

    sent.clear();
    mavlink_msg_file_transfer_protocol_pack(GCS_SYS, GCS_COMP, &msg, 0, 1, COMPID, payload);
    gcs(msg);
    server.tick(1);
    mavlink_msg_file_transfer_protocol_decode(&sent[0], &reply);
    TEST_ASSERT_EQUAL(128, reply.payload[3]); // ACK for ResetSessions
}

static void commandLong(uint16_t command, float p1)
{
    mavlink_message_t msg;
    mavlink_msg_command_long_pack(GCS_SYS, GCS_COMP, &msg, 1, COMPID, command, 0, p1, 0, 0, 0, 0, 0, 0);
    gcs(msg);
}

void test_autopilot_version_request()
{
    start();
    commandLong(MAV_CMD_REQUEST_MESSAGE, MAVLINK_MSG_ID_AUTOPILOT_VERSION);
    server.tick(0);
    TEST_ASSERT_EQUAL(1, countOf(MAVLINK_MSG_ID_COMMAND_ACK));
    TEST_ASSERT_EQUAL(1, countOf(MAVLINK_MSG_ID_AUTOPILOT_VERSION));
    mavlink_command_ack_t ack;
    mavlink_msg_command_ack_decode(&sent[0], &ack);
    TEST_ASSERT_EQUAL(MAV_RESULT_ACCEPTED, ack.result);
    TEST_ASSERT_EQUAL(GCS_SYS, ack.target_system);
    mavlink_autopilot_version_t version;
    mavlink_msg_autopilot_version_decode(&sent[1], &version);
    TEST_ASSERT_TRUE(version.capabilities & MAV_PROTOCOL_CAPABILITY_PARAM_FLOAT);
    TEST_ASSERT_TRUE(version.capabilities & MAV_PROTOCOL_CAPABILITY_MAVLINK2);
    TEST_ASSERT_FALSE(version.capabilities & MAV_PROTOCOL_CAPABILITY_FTP);
    TEST_ASSERT_EQUAL_HEX32(0x01020300, version.flight_sw_version);

    sent.clear();
    commandLong(MAV_CMD_COMPONENT_ARM_DISARM, 1);
    server.tick(1);
    mavlink_msg_command_ack_decode(&sent[0], &ack);
    TEST_ASSERT_EQUAL(MAV_RESULT_UNSUPPORTED, ack.result);
    TEST_ASSERT_EQUAL(MAV_CMD_COMPONENT_ARM_DISARM, ack.command);
}

void test_full_output_keeps_frames_pending_and_seq()
{
    start();
    requestList(1, COMPID);
    sendAccepts = false;
    server.tick(0);
    TEST_ASSERT_TRUE(server.hasPendingOutput());
    sendAccepts = true;
    server.tick(1);
    auto values = paramValues();
    TEST_ASSERT_EQUAL(3, values.size());
    // Sequence numbers continue without gaps from the frames that could not be sent
    uint8_t seq = sent[0].seq;
    for (auto &m : sent)
        TEST_ASSERT_EQUAL(seq++, m.seq);
    TEST_ASSERT_FALSE(server.hasPendingOutput());
}

void test_is_addressed_to_us()
{
    start();
    mavlink_message_t msg;
    mavlink_msg_param_set_pack(GCS_SYS, GCS_COMP, &msg, 1, COMPID, "TST_A", 1, MAV_PARAM_TYPE_REAL32);
    TEST_ASSERT_TRUE(server.isAddressedToUs(&msg));
    mavlink_msg_param_set_pack(GCS_SYS, GCS_COMP, &msg, 1, 0, "TST_A", 1, MAV_PARAM_TYPE_REAL32);
    TEST_ASSERT_FALSE(server.isAddressedToUs(&msg)); // broadcast: the FC needs it too
    mavlink_msg_param_set_pack(GCS_SYS, GCS_COMP, &msg, 1, 1, "TST_A", 1, MAV_PARAM_TYPE_REAL32);
    TEST_ASSERT_FALSE(server.isAddressedToUs(&msg));
    mavlink_msg_heartbeat_pack(GCS_SYS, GCS_COMP, &msg, MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID, 0, 0, 0);
    TEST_ASSERT_FALSE(server.isAddressedToUs(&msg)); // no target fields
}

void test_send_as_component()
{
    start(5000);
    TEST_ASSERT_FALSE(server.ready());
    vehicleHeartbeat(7, 1, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_ARDUPILOTMEGA);
    server.tick(0);
    TEST_ASSERT_TRUE(server.ready());
    // Encoded by someone else, under another identity: sent as this component, next in sequence
    mavlink_message_t msg;
    mavlink_msg_named_value_float_pack(99, 99, &msg, 1234, "TEST", 1.5f);
    TEST_ASSERT_TRUE(server.sendAsComponent(&msg, MAVLINK_MSG_ID_NAMED_VALUE_FLOAT_MIN_LEN, MAVLINK_MSG_ID_NAMED_VALUE_FLOAT_CRC));
    TEST_ASSERT_EQUAL(2, sent.size()); // heartbeat, then ours; it parsed with a valid CRC
    TEST_ASSERT_EQUAL(MAVLINK_MSG_ID_NAMED_VALUE_FLOAT, sent[1].msgid);
    TEST_ASSERT_EQUAL(7, sent[1].sysid);
    TEST_ASSERT_EQUAL(COMPID, sent[1].compid);
    TEST_ASSERT_EQUAL((uint8_t)(sent[0].seq + 1), sent[1].seq);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, mavlink_msg_named_value_float_get_value(&sent[1]));
}


static std::vector<uint8_t> finishFrame(std::vector<uint8_t> f, uint8_t crcExtra)
{
    // f: header + payload; append the CRC (over everything but the magic byte, then crc_extra)
    uint16_t crc = crc_calculate(f.data() + 1, f.size() - 1);
    crc_accumulate(crcExtra, &crc);
    f.push_back(crc & 0xFF);
    f.push_back(crc >> 8);
    return f;
}

static std::vector<uint8_t> asForwarded(const std::vector<uint8_t> &in, uint8_t *result)
{
    mavlink_message_t buf, msg;
    mavlink_status_t st = {}, out;
    *result = 0;
    std::vector<uint8_t> fwd;
    for (uint8_t c : in)
    {
        uint8_t r = mavlink_frame_char_buffer(&buf, &st, c, &msg, &out);
        if (r)
        {
            *result = r;
            uint8_t hdr[MAVLINK_NUM_HEADER_BYTES];
            uint8_t n = mavFrameHeader(&msg, hdr);
            fwd.insert(fwd.end(), hdr, hdr + n);
            const uint8_t *p = (const uint8_t *)_MAV_PAYLOAD(&msg);
            fwd.insert(fwd.end(), p, p + msg.len);
            fwd.insert(fwd.end(), msg.ck, msg.ck + 2);
            fwd.insert(fwd.end(), msg.signature, msg.signature + mavFrameSignatureLen(&msg));
        }
    }
    return fwd;
}

static std::vector<uint8_t> packed(mavlink_message_t &msg)
{
    uint8_t b[MAVLINK_MAX_PACKET_LEN];
    uint16_t n = mavlink_msg_to_send_buffer(b, &msg);
    return std::vector<uint8_t>(b, b + n);
}

static std::vector<uint8_t> reserialised(const std::vector<uint8_t> &in)
{
    mavlink_message_t buf, msg;
    mavlink_status_t st = {}, out;
    for (uint8_t c : in)
    {
        if (mavlink_frame_char_buffer(&buf, &st, c, &msg, &out))
            return packed(msg);
    }
    return {};
}

void test_frames_forwarded_as_received()
{
    uint8_t r;
    mavlink_message_t msg;
    // MAVLink 2, known message
    mavlink_msg_heartbeat_pack(1, 1, &msg, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_ARDUPILOTMEGA, 0, 0, MAV_STATE_ACTIVE);
    std::vector<uint8_t> v2 = packed(msg);
    TEST_ASSERT_TRUE(asForwarded(v2, &r) == v2);
    TEST_ASSERT_EQUAL(MAVLINK_FRAMING_OK, r);

    // MAVLink 1
    mavlink_status_t st1 = {};
    st1.flags = MAVLINK_STATUS_FLAG_OUT_MAVLINK1;
    mavlink_heartbeat_t hb = {};
    hb.type = MAV_TYPE_GCS;
    mavlink_msg_heartbeat_encode_status(255, 190, &st1, &msg, &hb);
    std::vector<uint8_t> v1 = packed(msg);
    TEST_ASSERT_EQUAL(MAVLINK_STX_MAVLINK1, v1[0]);
    TEST_ASSERT_TRUE(asForwarded(v1, &r) == v1);

    // Untrimmed MAVLink 2 (trailing zeros left in, valid CRC)
    std::vector<uint8_t> un = {MAVLINK_STX, MAVLINK_MSG_ID_COMMAND_LONG_LEN, 0, 0, 7, 255, 190,
                               MAVLINK_MSG_ID_COMMAND_LONG & 0xFF, 0, 0};
    for (uint8_t i = 0; i < MAVLINK_MSG_ID_COMMAND_LONG_LEN; i++)
        un.push_back(i < 4 ? 0x3F : 0);
    un = finishFrame(un, MAVLINK_MSG_ID_COMMAND_LONG_CRC);
    TEST_ASSERT_TRUE(asForwarded(un, &r) == un);
    TEST_ASSERT_EQUAL(MAVLINK_FRAMING_OK, r);

    // Corrupted (bad CRC): keeps its bad CRC, so the FC still rejects it
    std::vector<uint8_t> bad = v2;
    bad[12] ^= 0x55;
    TEST_ASSERT_TRUE(asForwarded(bad, &r) == bad);
    TEST_ASSERT_EQUAL(MAVLINK_FRAMING_BAD_CRC, r);

    // A message this dialect doesn't know (MLRS_RADIO_LINK_STATS, storm32), valid for its own CRC extra
    std::vector<uint8_t> other = {MAVLINK_STX, 5, 0, 0, 9, 1, 241, 60045 & 0xFF, (60045 >> 8) & 0xFF, 0, 1, 2, 3, 4, 5};
    other = finishFrame(other, 14);
    TEST_ASSERT_TRUE(asForwarded(other, &r) == other);

    // Signed: the signature goes along
    std::vector<uint8_t> sig(v2.begin(), v2.end() - 2);
    sig[2] |= MAVLINK_IFLAG_SIGNED;
    sig = finishFrame(sig, MAVLINK_MSG_ID_HEARTBEAT_CRC);
    for (uint8_t i = 0; i < MAVLINK_SIGNATURE_BLOCK_LEN; i++)
        sig.push_back(0xA0 + i);
    TEST_ASSERT_TRUE(asForwarded(sig, &r) == sig);

    // Re-serialising would trim the untrimmed frame but keep its checksum. The parser puts the wire
    // checksum back for bad-CRC and other-dialect frames, so those would survive it.
    TEST_ASSERT_FALSE(reserialised(un) == un);
    TEST_ASSERT_TRUE(reserialised(bad) == bad);
    TEST_ASSERT_TRUE(reserialised(other) == other);
}

void test_slow_heartbeat_and_heartbeat_on_request()
{
    // As the receiver: every 10 s, plus one at once when its link comes up
    heartbeatInterval = 10000;
    start(5000);
    vehicleHeartbeat(1, 1, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_ARDUPILOTMEGA);
    server.tick(0);
    TEST_ASSERT_EQUAL(1, countOf(MAVLINK_MSG_ID_HEARTBEAT));
    server.tick(5000);
    server.tick(9999);
    TEST_ASSERT_EQUAL(1, countOf(MAVLINK_MSG_ID_HEARTBEAT));
    server.tick(10000);
    TEST_ASSERT_EQUAL(2, countOf(MAVLINK_MSG_ID_HEARTBEAT));
    server.sendHeartbeatSoon();   // link came back
    server.tick(12000);
    TEST_ASSERT_EQUAL(3, countOf(MAVLINK_MSG_ID_HEARTBEAT));
    server.tick(21999);
    TEST_ASSERT_EQUAL(3, countOf(MAVLINK_MSG_ID_HEARTBEAT));
    server.tick(22000);
    TEST_ASSERT_EQUAL(4, countOf(MAVLINK_MSG_ID_HEARTBEAT));
}

void test_version_from_string()
{
    TEST_ASSERT_EQUAL_HEX32(0x04010200, MavParamServer::versionFromString("4.1.2"));
    TEST_ASSERT_EQUAL_HEX32(0x0A000F00, MavParamServer::versionFromString("10.0.15-rc1"));
    TEST_ASSERT_EQUAL_HEX32(0, MavParamServer::versionFromString("add-usb-config"));
    TEST_ASSERT_EQUAL_HEX32(0, MavParamServer::versionFromString("4.1"));
    TEST_ASSERT_EQUAL_HEX32(0, MavParamServer::versionFromString("4.1.300"));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_unavailable_params_are_not_listed);
    RUN_TEST(test_heartbeat_waits_for_vehicle_then_uses_fallback);
    RUN_TEST(test_heartbeat_starts_with_vehicle_and_identifies_us);
    RUN_TEST(test_sysid_latched_to_first_autopilot);
    RUN_TEST(test_request_list_paced_and_complete);
    RUN_TEST(test_request_list_addressing);
    RUN_TEST(test_request_read_by_name_and_index);
    RUN_TEST(test_set_replies_after_delay_with_changed_values);
    RUN_TEST(test_set_must_be_addressed_exactly);
    RUN_TEST(test_set_locked_is_refused_with_reason);
    RUN_TEST(test_set_refused_by_setter);
    RUN_TEST(test_set_read_only);
    RUN_TEST(test_ftp_is_refused);
    RUN_TEST(test_autopilot_version_request);
    RUN_TEST(test_full_output_keeps_frames_pending_and_seq);
    RUN_TEST(test_is_addressed_to_us);
    RUN_TEST(test_send_as_component);
    RUN_TEST(test_frames_forwarded_as_received);
    RUN_TEST(test_slow_heartbeat_and_heartbeat_on_request);
    RUN_TEST(test_version_from_string);
    UNITY_END();
    return 0;
}
