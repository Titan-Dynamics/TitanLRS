#if defined(TARGET_RX)

#include "rx_mavlink.h"

#include "common.h"
#include "config.h"
#include "deferred.h"
#include "options.h"
#include "POWERMGNT.h"

#include "MavParamServer.h"
#include "common/mavlink.h"

// No heartbeat for this long after start unless the vehicle is seen, then the Target SysID
#define HEARTBEAT_WAIT 5000
// The heartbeat goes over the air: one when the link comes up (so the GCS finds the receiver), then
// this rarely. Mission Planner keeps a component once it has seen its heartbeat.
#define HEARTBEAT_INTERVAL 10000
// Room left in the downlink queue for FC frames when the RX adds one of its own
#define DOWNLINK_RESERVE 64
// Longest a config commit is held for the reply to a parameter write
#define COMMIT_HOLD_MAX 1000

extern void reconfigureSerial1();

static MavParamServer server;
static bool started;
static FIFO<MAV_INPUT_BUF_LEN> *downlinkQueue;
static const uint16_t *fcFrameHeld;
// Bytes in the downlink queue up to and including the RX's last frame
static uint16_t downlinkAhead;
static uint32_t lastSetMs;
static bool setSinceCommit;

static const uint16_t powerLevelsMw[PWR_COUNT] = {10, 25, 50, 100, 250, 500, 1000, 2000};

static bool isWhole(float value, float max)
{
    return value >= 0 && value <= max && value == (float)(uint16_t)value;
}

// Every accepted write ends in a config commit, which drops the link for a moment
static void accepted()
{
    lastSetMs = millis();
    setSinceCommit = true;
}

// The doc comments on each entry are read by python/gen_param_metadata.py

static float getProtocol() { return config.GetSerialProtocol(); }

#if defined(PLATFORM_ESP32)
#define RX_HAS_SERIAL1 (GPIO_PIN_SERIAL1_TX != UNDEF_PIN || OPT_HAS_SERVO_OUTPUT)

static bool hasProtocol2() { return RX_HAS_SERIAL1; }
static float getProtocol2() { return config.GetSerial1Protocol(); }
static const char *setProtocol2(float value)
{
    if (!isWhole(value, PROTOCOL_SERIAL1_GPS))
        return "not a protocol";
    config.SetSerial1Protocol((eSerial1Protocol)value);
    if (config.IsModified())
    {
        deferExecutionMillis(100, []() { reconfigureSerial1(); });
    }
    accepted();
    return nullptr;
}
#endif

static bool hasTlmPower() { return POWERMGNT::getMinPower() != POWERMGNT::getMaxPower(); }
static float getTlmPower()
{
    const uint8_t power = config.GetPower();
    return power < PWR_COUNT ? powerLevelsMw[power] : 0;
}
static const char *setTlmPower(float value)
{
    if (value == 0)
    {
        config.SetPower(PWR_MATCH_TX);
        accepted();
        return nullptr;
    }
    for (uint8_t power = POWERMGNT::getMinPower(); power <= POWERMGNT::getMaxPower(); power++)
    {
        if (value == powerLevelsMw[power])
        {
            // Applied by updatePower() in the main loop, as when set from Lua
            config.SetPower(power);
            accepted();
            return nullptr;
        }
    }
    return "not a power level of this RX";
}

// A receiver with two radios takes its antenna mode from the TX (TX_ANT_MODE)
static bool hasAntennaMode() { return GPIO_PIN_ANT_CTRL != UNDEF_PIN && !isDualRadio(); }
static float getAntennaMode() { return config.GetAntennaMode(); }
static const char *setAntennaMode(float value)
{
    if (!isWhole(value, 2))
        return "not an antenna mode";
    config.SetAntennaMode((uint8_t)value);
    accepted();
    return nullptr;
}

static float getTargetSysid() { return config.GetTargetSysId() ? config.GetTargetSysId() : 1; }
static const char *setTargetSysid(float value)
{
    if (!isWhole(value, 255) || value == 0)
        return "not a system ID";
    // SerialMavlink picks it up from the config change event after the commit
    config.SetTargetSysId((uint8_t)value);
    accepted();
    return nullptr;
}

static float getSourceSysid() { return config.GetSourceSysId() ? config.GetSourceSysId() : 255; }
static const char *setSourceSysid(float value)
{
    if (!isWhole(value, 255) || value == 0)
        return "not a system ID";
    config.SetSourceSysId((uint8_t)value);
    accepted();
    return nullptr;
}

static float getModelId() { return config.GetModelId(); }

static const MavParam params[] = {
    // @Param: RX_PROTOCOL
    // @DisplayName: Serial protocol
    // @Description: Protocol on the receiver's serial port. Follows the TX link mode: MAVLink while the TX is in MAVLink mode, CRSF otherwise. Change TX_LINK_MODE to leave MAVLink.
    // @Values: 0:CRSF,1:Inverted CRSF,2:SBUS,3:Inverted SBUS,4:SUMD,5:DJI RS Pro,6:HoTT Telemetry,7:MAVLink,8:DisplayPort,9:GPS
    // @ReadOnly: True
    // @User: Standard
    {"RX_PROTOCOL", nullptr, getProtocol, nullptr, nullptr},
#if defined(PLATFORM_ESP32)
    // @Param: RX_PROTOCOL2
    // @DisplayName: Serial 2 protocol
    // @Description: Protocol on the receiver's second serial port.
    // @Values: 0:Off,1:CRSF,2:Inverted CRSF,3:SBUS,4:Inverted SBUS,5:SUMD,6:DJI RS Pro,7:HoTT Telemetry,8:Tramp,9:SmartAudio,10:DisplayPort,11:GPS
    // @Hardware: ESP32 receivers with a second serial port only
    // @User: Advanced
    {"RX_PROTOCOL2", hasProtocol2, getProtocol2, setProtocol2, nullptr},
#endif
    // @Param: RX_TLM_POWER
    // @DisplayName: Telemetry power
    // @Description: Receiver output power for telemetry. 0 matches the TX power. Writing it drops the link for a moment while the receiver saves.
    // @Values: 0:Match TX,10:10 mW,25:25 mW,50:50 mW,100:100 mW,250:250 mW,500:500 mW,1000:1000 mW,2000:2000 mW
    // @Units: mW
    // @Hardware: Each receiver supports a range of these levels
    // @User: Standard
    {"RX_TLM_POWER", hasTlmPower, getTlmPower, setTlmPower, nullptr},
    // @Param: RX_ANT_MODE
    // @DisplayName: Antenna mode
    // @Description: Antenna the receiver uses. Writing it drops the link for a moment while the receiver saves.
    // @Values: 0:Antenna A,1:Antenna B,2:Diversity
    // @Hardware: Single-radio receivers with an antenna switch only; receivers with two radios follow TX_ANT_MODE
    // @User: Advanced
    {"RX_ANT_MODE", hasAntennaMode, getAntennaMode, setAntennaMode, nullptr},
    // @Param: RX_TGT_SYSID
    // @DisplayName: Target system ID
    // @Description: System ID of the flight controller the receiver sends RC_CHANNELS_OVERRIDE to. Writing it drops the link for a moment while the receiver saves.
    // @Range: 1 255
    // @User: Advanced
    {"RX_TGT_SYSID", nullptr, getTargetSysid, setTargetSysid, nullptr},
    // @Param: RX_SRC_SYSID
    // @DisplayName: Source system ID
    // @Description: System ID the receiver sends RC_CHANNELS_OVERRIDE and RADIO_STATUS from. Must match the flight controller's GCS system ID (ArduPilot MAV_GCS_SYSID / SYSID_MYGCS) for the overrides to be used. Writing it drops the link for a moment while the receiver saves.
    // @Range: 1 255
    // @User: Advanced
    {"RX_SRC_SYSID", nullptr, getSourceSysid, setSourceSysid, nullptr},
    // @Param: RX_MODEL_ID
    // @DisplayName: Model ID
    // @Description: Model ID the receiver is bound to for Model Match. 255 means Model Match is off.
    // @ReadOnly: True
    // @User: Advanced
    {"RX_MODEL_ID", nullptr, getModelId, nullptr, nullptr},
};


static bool queueFrame(const uint8_t *frame, uint16_t len)
{
    // FC frames are only ever queued whole (SerialMavlink::processBytes), so a frame added here
    // always lands between two of them. The room for an FC frame being received is not ours to use.
    if (downlinkQueue == nullptr || downlinkQueue->free() < len + DOWNLINK_RESERVE + *fcFrameHeld)
        return false;
    downlinkQueue->atomicPushBytes(frame, len);
    downlinkAhead = downlinkQueue->size();
    return true;
}

static uint8_t fallbackSysid() { return config.GetTargetSysId() ? config.GetTargetSysId() : 1; }

static void start()
{
    if (started)
        return;
    started = true;
    MavParamServer::Config cfg = {};
    cfg.compid = TLRS_MAV_RX_COMPID;
    cfg.fallbackSysid = fallbackSysid;
    cfg.heartbeatWaitMs = HEARTBEAT_WAIT;
    cfg.heartbeatIntervalMs = HEARTBEAT_INTERVAL;
    cfg.replyDelayMs = 0;
    // Downlink airtime is shared with the FC's telemetry
    cfg.maxFramesPerTick = 1;
    cfg.swVersion = MavParamServer::versionFromString(version);
    server.begin(params, sizeof(params) / sizeof(params[0]), cfg, queueFrame, millis());
}

void RxMavlink_Attach(FIFO<MAV_INPUT_BUF_LEN> *downlink, const uint16_t *fcFrameHeldBytes)
{
    start();
    downlinkQueue = downlink;
    fcFrameHeld = fcFrameHeldBytes;
    downlinkAhead = 0;
}

void RxMavlink_Detach()
{
    downlinkQueue = nullptr;
    fcFrameHeld = nullptr;
    downlinkAhead = 0;
}

void RxMavlink_HandleVehicle(const mavlink_message_t *msg)
{
    server.handleVehicleMessage(msg);
}

void RxMavlink_HandleGcs(const mavlink_message_t *msg, uint32_t now)
{
    server.handleGcsMessage(msg, now);
}

bool RxMavlink_IsAddressedToRx(const mavlink_message_t *msg)
{
    return server.isAddressedToUs(msg);
}

void RxMavlink_DownlinkPopped(uint16_t count)
{
    downlinkAhead = count < downlinkAhead ? downlinkAhead - count : 0;
}

void RxMavlink_Tick(uint32_t now, bool connected)
{
    static bool wasConnected;
    if (connected && !wasConnected)
    {
        server.sendHeartbeatSoon();
    }
    wasConnected = connected;
    if (connected)
    {
        server.tick(now);
    }
}

bool RxMavlink_HoldCommit(uint32_t now, bool downlinkBusy)
{
    if (!setSinceCommit)
        return false;
    if (now - lastSetMs < COMMIT_HOLD_MAX && (server.hasPendingOutput() || downlinkAhead != 0 || downlinkBusy))
        return true;
    setSinceCommit = false;
    return false;
}

#endif // TARGET_RX
