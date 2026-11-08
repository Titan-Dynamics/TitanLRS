#pragma once

#include <stdint.h>

/*
 * A MAVLink component that exposes a table of settings through the parameter protocol. Platform
 * independent: the owner feeds it decoded messages and the time, and supplies the output.
 *
 * MAVFTP requests are NAKed, so a GCS probing for it falls back to the parameter protocol at once.
 * The system ID is taken from the vehicle's autopilot HEARTBEAT and kept for the session, so other
 * vehicles or components on the feed can't move it.
 */

struct __mavlink_message;

// HEARTBEAT.custom_mode, ASCII "TLRS": how Titan Planner recognises the component
#define TLRS_MAV_HEARTBEAT_MARKER 0x544C5253UL

#define MAV_PARAM_SERVER_MAX_PARAMS 24
// Table entries beyond this are ignored
#define MAV_PARAM_SERVER_MAX_TABLE 32
#define MAV_PARAM_ID_LEN 16
#define MAV_PARAM_STATUSTEXT_LEN 50

struct MavParam
{
    // Up to 16 characters
    const char *id;
    // Checked once, in begin(). nullptr: always available.
    bool (*available)();
    float (*get)();
    // Returns nullptr if accepted, or the reason it was refused (sent as STATUSTEXT). nullptr: read-only.
    const char *(*set)(float value);
    // Returns why the parameter can't be written right now, or nullptr. nullptr: never locked.
    const char *(*locked)();
    // Whether it is listed now, for parameters that only apply while an _ENABLE parameter is set, as
    // with ArduPilot's. Rechecked when a set is answered, so that reply carries the new count and the
    // GCS knows to refresh its list, and between exchanges, but never during a list download. A
    // hidden parameter can't be read or set. nullptr: always shown.
    bool (*shown)();
};

class MavParamServer
{
public:
    typedef bool (*SendFn)(const uint8_t *frame, uint16_t len);

    struct Config
    {
        uint8_t compid;
        // Used until the vehicle has been seen
        uint8_t (*fallbackSysid)();
        // No HEARTBEAT until the vehicle has been seen, or this long after begin()
        uint32_t heartbeatWaitMs;
        // 0 for 1 s
        uint32_t heartbeatIntervalMs;
        // For settings that are applied after the PARAM_SET is accepted
        uint32_t replyDelayMs;
        uint8_t maxFramesPerTick;
        // AUTOPILOT_VERSION.flight_sw_version
        uint32_t swVersion;
    };

    // Returns the number of parameters listed now
    uint8_t begin(const MavParam *table, uint8_t tableLen, const Config &config, SendFn send, uint32_t now);

    void handleGcsMessage(const __mavlink_message *msg, uint32_t now);
    // Learns the vehicle's system ID
    void handleVehicleMessage(const __mavlink_message *msg);
    void tick(uint32_t now);
    // Takes effect once the system ID is known
    void sendHeartbeatSoon() { heartbeatDue_ = true; }

    uint8_t sysid() const;
    // 0 until the vehicle has been seen
    uint8_t vehicleSysid() const { return vehicleSysid_; }
    uint8_t compid() const { return config_.compid; }
    uint8_t paramCount() const { return count_; }
    const MavParam *param(uint8_t index) const { return index < count_ ? &table_[listed_[index]] : nullptr; }
    // Includes a reply that isn't due yet
    bool hasPendingOutput() const;
    // Broadcasts don't count
    bool isAddressedToUs(const __mavlink_message *msg) const;

    // Other messages should wait until the component knows its system ID
    bool ready() const { return heartbeatStarted_; }
    // Fills in this component's IDs and sequence number. The caller gives the minimum length and CRC
    // extra, for messages outside the dialect compiled in here.
    bool sendAsComponent(__mavlink_message *msg, uint8_t minLength, uint8_t crcExtra);

    // "4.1.2..." -> 0x04010200 (AUTOPILOT_VERSION.flight_sw_version layout), 0 if not a version
    static uint32_t versionFromString(const char *version);

private:
    void relist();
    bool forUs(uint8_t targetSystem, uint8_t targetComponent, bool allowBroadcast) const;
    // Table index of a listed parameter, or -1
    int findParam(const char *id) const;
    // Index in the list, or -1 while hidden
    int listIndex(uint8_t tableIndex) const;
    void rejectSet(uint8_t index, const char *reason);
    void handleCommandLong(const __mavlink_message *msg);
    void handleFtp(const __mavlink_message *msg);
    bool sendMessage(__mavlink_message *msg);
    bool sendHeartbeat();
    bool sendParamValue(uint8_t index);
    bool sendStatusText();
    bool sendCommandAck();
    bool sendAutopilotVersion();
    bool sendFtpReply();

    const MavParam *table_ = nullptr;
    uint8_t tableLen_ = 0;
    // Table entries this hardware has
    uint32_t available_ = 0;
    // Everything below is by table index
    Config config_ = {};
    SendFn send_ = nullptr;
    uint8_t listed_[MAV_PARAM_SERVER_MAX_PARAMS] = {};
    uint8_t count_ = 0;

    uint8_t vehicleSysid_ = 0;
    uint32_t startMs_ = 0;
    uint32_t lastHeartbeatMs_ = 0;
    bool heartbeatStarted_ = false;
    bool heartbeatDue_ = false;
    uint8_t txSeq_ = 0;

    uint32_t paramMask_ = 0;
    // The reply to a PARAM_SET covers every parameter whose value changed with it
    bool setReplyPending_ = false;
    uint8_t setIndex_ = 0;
    uint32_t setReplyAt_ = 0;
    float snapshot_[MAV_PARAM_SERVER_MAX_TABLE] = {};

    bool statusTextPending_ = false;
    char statusText_[MAV_PARAM_STATUSTEXT_LEN] = {};

    bool ackPending_ = false;
    uint16_t ackCommand_ = 0;
    uint8_t ackResult_ = 0;
    uint8_t ackSystem_ = 0;
    uint8_t ackComponent_ = 0;
    bool versionPending_ = false;

    bool ftpPending_ = false;
    uint8_t ftpSystem_ = 0;
    uint8_t ftpComponent_ = 0;
    uint8_t ftpReply_[13] = {};
};
