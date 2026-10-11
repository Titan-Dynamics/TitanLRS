#include "MavParamServer.h"

#include <string.h>

#include "common/mavlink.h"

#define DEFAULT_HEARTBEAT_INTERVAL_MS 1000
#define STATUSTEXT_SEVERITY MAV_SEVERITY_NOTICE

// MAVLink FTP (https://mavlink.io/en/services/ftp.html): request/reply header layout and codes
#define FTP_OFS_SEQ 0
#define FTP_OFS_SESSION 2
#define FTP_OFS_OPCODE 3
#define FTP_OFS_SIZE 4
#define FTP_OFS_REQ_OPCODE 5
#define FTP_OFS_DATA 12
#define FTP_OP_TERMINATE_SESSION 1
#define FTP_OP_RESET_SESSIONS 2
#define FTP_OP_ACK 128
#define FTP_OP_NAK 129
#define FTP_ERR_UNKNOWN_COMMAND 7
#define FTP_ERR_FILE_NOT_FOUND 10

static bool timeReached(uint32_t now, uint32_t at)
{
    return (int32_t)(now - at) >= 0;
}

uint32_t MavParamServer::versionFromString(const char *version)
{
    uint32_t parts[3] = {};
    for (uint8_t i = 0; i < 3; i++)
    {
        if (*version < '0' || *version > '9')
            return 0;
        while (*version >= '0' && *version <= '9')
            parts[i] = parts[i] * 10 + (*version++ - '0');
        if (i < 2 && *version++ != '.')
            return 0;
    }
    if (parts[0] > 255 || parts[1] > 255 || parts[2] > 255)
        return 0;
    return (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8);
}

uint8_t MavParamServer::begin(const MavParam *table, uint8_t tableLen, const Config &config, SendFn send, uint32_t now)
{
    table_ = table;
    config_ = config;
    send_ = send;
    startMs_ = now;
    count_ = 0;
    for (uint8_t i = 0; i < tableLen && count_ < MAV_PARAM_SERVER_MAX_PARAMS; i++)
    {
        if (table[i].available == nullptr || table[i].available())
        {
            listed_[count_++] = i;
        }
    }
    return count_;
}

uint8_t MavParamServer::sysid() const
{
    if (vehicleSysid_ != 0)
        return vehicleSysid_;
    return config_.fallbackSysid ? config_.fallbackSysid() : 1;
}

bool MavParamServer::hasPendingOutput() const
{
    return paramMask_ != 0 || setReplyPending_ || statusTextPending_ || ackPending_ || versionPending_ || ftpPending_;
}

bool MavParamServer::forUs(uint8_t targetSystem, uint8_t targetComponent, bool allowBroadcast) const
{
    const bool system = targetSystem == sysid() || (allowBroadcast && targetSystem == 0);
    const bool component = targetComponent == config_.compid || (allowBroadcast && targetComponent == MAV_COMP_ID_ALL);
    return system && component;
}

bool MavParamServer::isAddressedToUs(const mavlink_message_t *msg) const
{
    const mavlink_msg_entry_t *entry = mavlink_get_msg_entry(msg->msgid);
    if (entry == nullptr || !(entry->flags & MAV_MSG_ENTRY_FLAG_HAVE_TARGET_COMPONENT) ||
        !(entry->flags & MAV_MSG_ENTRY_FLAG_HAVE_TARGET_SYSTEM))
    {
        return false;
    }
    // Trimmed MAVLink 2 payloads read as zero past their length, which is never our address
    const uint8_t *payload = (const uint8_t *)_MAV_PAYLOAD(msg);
    const uint8_t targetSystem = entry->target_system_ofs < msg->len ? payload[entry->target_system_ofs] : 0;
    const uint8_t targetComponent = entry->target_component_ofs < msg->len ? payload[entry->target_component_ofs] : 0;
    return targetSystem == sysid() && targetComponent == config_.compid;
}

int MavParamServer::findParam(const char *id) const
{
    for (uint8_t i = 0; i < count_; i++)
    {
        if (strncmp(id, table_[listed_[i]].id, MAV_PARAM_ID_LEN) == 0)
            return i;
    }
    return -1;
}

void MavParamServer::handleVehicleMessage(const mavlink_message_t *msg)
{
    if (vehicleSysid_ != 0 || msg->msgid != MAVLINK_MSG_ID_HEARTBEAT)
        return;
    // The first autopilot heard is the vehicle. Companion computers, gimbals, radios and GCSs
    // report MAV_AUTOPILOT_INVALID or MAV_TYPE_GCS.
    if (mavlink_msg_heartbeat_get_autopilot(msg) != MAV_AUTOPILOT_INVALID &&
        mavlink_msg_heartbeat_get_type(msg) != MAV_TYPE_GCS && msg->sysid != 0)
    {
        vehicleSysid_ = msg->sysid;
    }
}

void MavParamServer::rejectSet(uint8_t index, const char *reason)
{
    // Echo the unchanged value, which is how the parameter protocol reports a refused write
    paramMask_ |= 1UL << index;
    strncpy(statusText_, table_[listed_[index]].id, sizeof(statusText_) - 1);
    statusText_[sizeof(statusText_) - 1] = '\0';
    strncat(statusText_, ": ", sizeof(statusText_) - strlen(statusText_) - 1);
    strncat(statusText_, reason, sizeof(statusText_) - strlen(statusText_) - 1);
    statusTextPending_ = true;
}

void MavParamServer::handleGcsMessage(const mavlink_message_t *msg, uint32_t now)
{
    if (count_ == 0 && msg->msgid != MAVLINK_MSG_ID_COMMAND_LONG && msg->msgid != MAVLINK_MSG_ID_FILE_TRANSFER_PROTOCOL)
        return;

    switch (msg->msgid)
    {
    case MAVLINK_MSG_ID_PARAM_REQUEST_LIST: {
        mavlink_param_request_list_t req;
        mavlink_msg_param_request_list_decode(msg, &req);
        if (forUs(req.target_system, req.target_component, true))
        {
            // A repeated request starts the list again
            paramMask_ = count_ >= 32 ? 0xFFFFFFFFUL : (1UL << count_) - 1;
        }
        break;
    }
    case MAVLINK_MSG_ID_PARAM_REQUEST_READ: {
        mavlink_param_request_read_t req;
        mavlink_msg_param_request_read_decode(msg, &req);
        if (forUs(req.target_system, req.target_component, true))
        {
            char id[MAV_PARAM_ID_LEN + 1] = {};
            memcpy(id, req.param_id, MAV_PARAM_ID_LEN);
            const int index = req.param_index >= 0 ? (req.param_index < count_ ? req.param_index : -1) : findParam(id);
            if (index >= 0)
            {
                paramMask_ |= 1UL << index;
            }
        }
        break;
    }
    case MAVLINK_MSG_ID_PARAM_SET: {
        mavlink_param_set_t set;
        mavlink_msg_param_set_decode(msg, &set);
        if (!forUs(set.target_system, set.target_component, false))
            break;
        char id[MAV_PARAM_ID_LEN + 1] = {};
        memcpy(id, set.param_id, MAV_PARAM_ID_LEN);
        const int index = findParam(id);
        if (index < 0)
            break;
        const MavParam &param = table_[listed_[index]];
        const char *reason = nullptr;
        if (param.set == nullptr)
        {
            reason = "read-only";
        }
        else if (param.locked != nullptr)
        {
            reason = param.locked();
        }
        if (reason == nullptr)
        {
            for (uint8_t i = 0; i < count_; i++)
            {
                snapshot_[i] = table_[listed_[i]].get();
            }
            reason = param.set(set.param_value);
        }
        if (reason != nullptr)
        {
            rejectSet(index, reason);
            break;
        }
        // Reply once the setting has taken effect, with every value it changed
        if (setReplyPending_ && setIndex_ != index)
        {
            paramMask_ |= 1UL << setIndex_;
        }
        setReplyPending_ = true;
        setIndex_ = index;
        setReplyAt_ = now + config_.replyDelayMs;
        break;
    }
    case MAVLINK_MSG_ID_COMMAND_LONG:
        handleCommandLong(msg);
        break;
    case MAVLINK_MSG_ID_FILE_TRANSFER_PROTOCOL:
        handleFtp(msg);
        break;
    default:
        break;
    }
}

void MavParamServer::handleCommandLong(const mavlink_message_t *msg)
{
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    if (!forUs(cmd.target_system, cmd.target_component, false))
        return;

    const bool versionRequested =
        (cmd.command == MAV_CMD_REQUEST_MESSAGE && (uint32_t)cmd.param1 == MAVLINK_MSG_ID_AUTOPILOT_VERSION) ||
        (cmd.command == MAV_CMD_REQUEST_AUTOPILOT_CAPABILITIES && cmd.param1 == 1);
    ackPending_ = true;
    ackCommand_ = cmd.command;
    ackResult_ = versionRequested ? MAV_RESULT_ACCEPTED : MAV_RESULT_UNSUPPORTED;
    ackSystem_ = msg->sysid;
    ackComponent_ = msg->compid;
    versionPending_ |= versionRequested;
}

void MavParamServer::handleFtp(const mavlink_message_t *msg)
{
    mavlink_file_transfer_protocol_t ftp;
    mavlink_msg_file_transfer_protocol_decode(msg, &ftp);
    if (!forUs(ftp.target_system, ftp.target_component, false))
        return;

    // No files here. Session resets are acknowledged (Mission Planner starts with one), every other
    // request is refused, so the GCS falls back to the parameter protocol at once.
    const uint8_t opcode = ftp.payload[FTP_OFS_OPCODE];
    const uint16_t seq = ftp.payload[FTP_OFS_SEQ] | (ftp.payload[FTP_OFS_SEQ + 1] << 8);
    memset(ftpReply_, 0, sizeof(ftpReply_));
    ftpReply_[FTP_OFS_SEQ] = (uint8_t)(seq + 1);
    ftpReply_[FTP_OFS_SEQ + 1] = (uint8_t)((seq + 1) >> 8);
    ftpReply_[FTP_OFS_SESSION] = ftp.payload[FTP_OFS_SESSION];
    ftpReply_[FTP_OFS_REQ_OPCODE] = opcode;
    if (opcode == FTP_OP_TERMINATE_SESSION || opcode == FTP_OP_RESET_SESSIONS)
    {
        ftpReply_[FTP_OFS_OPCODE] = FTP_OP_ACK;
    }
    else
    {
        ftpReply_[FTP_OFS_OPCODE] = FTP_OP_NAK;
        ftpReply_[FTP_OFS_SIZE] = 1;
        // Path operations (list, open, create, remove, ...) find nothing; the rest are not supported
        ftpReply_[FTP_OFS_DATA] = (opcode >= 3 && opcode <= 13 && opcode != 5 && opcode != 7) ? FTP_ERR_FILE_NOT_FOUND : FTP_ERR_UNKNOWN_COMMAND;
    }
    ftpSystem_ = msg->sysid;
    ftpComponent_ = msg->compid;
    ftpPending_ = true;
}

bool MavParamServer::sendMessage(mavlink_message_t *msg)
{
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    const uint16_t len = mavlink_msg_to_send_buffer(buf, msg);
    if (!send_(buf, len))
    {
        // Not sent, so the sequence number is not used up
        txSeq_--;
        return false;
    }
    return true;
}

bool MavParamServer::sendAsComponent(mavlink_message_t *msg, uint8_t minLength, uint8_t crcExtra)
{
    if (send_ == nullptr)
        return false;
    mavlink_status_t status = {};
    status.current_tx_seq = txSeq_++;
    mavlink_finalize_message_buffer(msg, sysid(), config_.compid, &status, minLength, msg->len, crcExtra);
    return sendMessage(msg);
}

// The library's encoders take a status struct only for the outgoing sequence number and flags
#define ENCODE(name, msg, data)                                         \
    mavlink_status_t status = {};                                       \
    status.current_tx_seq = txSeq_++;                                   \
    mavlink_msg_##name##_encode_status(sysid(), config_.compid, &status, msg, data)

bool MavParamServer::sendHeartbeat()
{
    mavlink_heartbeat_t heartbeat = {};
    heartbeat.custom_mode = TLRS_MAV_HEARTBEAT_MARKER;
    heartbeat.type = MAV_TYPE_GENERIC;
    // Not an autopilot, so a GCS never takes this component for the vehicle
    heartbeat.autopilot = MAV_AUTOPILOT_INVALID;
    heartbeat.system_status = MAV_STATE_ACTIVE;
    heartbeat.mavlink_version = 3;
    mavlink_message_t msg;
    ENCODE(heartbeat, &msg, &heartbeat);
    return sendMessage(&msg);
}

bool MavParamServer::sendParamValue(uint8_t index)
{
    mavlink_param_value_t value = {};
    value.param_value = table_[listed_[index]].get();
    value.param_count = count_;
    value.param_index = index;
    strncpy(value.param_id, table_[listed_[index]].id, MAV_PARAM_ID_LEN);
    // Whole numbers carried as float: exact for every value used, and read the same by every GCS
    value.param_type = MAV_PARAM_TYPE_REAL32;
    mavlink_message_t msg;
    ENCODE(param_value, &msg, &value);
    return sendMessage(&msg);
}

bool MavParamServer::sendStatusText()
{
    mavlink_statustext_t text = {};
    text.severity = STATUSTEXT_SEVERITY;
    strncpy(text.text, statusText_, sizeof(text.text));
    mavlink_message_t msg;
    ENCODE(statustext, &msg, &text);
    return sendMessage(&msg);
}

bool MavParamServer::sendCommandAck()
{
    mavlink_command_ack_t ack = {};
    ack.command = ackCommand_;
    ack.result = ackResult_;
    ack.target_system = ackSystem_;
    ack.target_component = ackComponent_;
    mavlink_message_t msg;
    ENCODE(command_ack, &msg, &ack);
    return sendMessage(&msg);
}

bool MavParamServer::sendAutopilotVersion()
{
    mavlink_autopilot_version_t version = {};
    // Parameters as plain floats, and no MAVFTP, so the GCS uses PARAM_REQUEST_LIST
    version.capabilities = MAV_PROTOCOL_CAPABILITY_PARAM_FLOAT | MAV_PROTOCOL_CAPABILITY_MAVLINK2;
    version.flight_sw_version = config_.swVersion;
    mavlink_message_t msg;
    ENCODE(autopilot_version, &msg, &version);
    return sendMessage(&msg);
}

bool MavParamServer::sendFtpReply()
{
    mavlink_file_transfer_protocol_t ftp = {};
    ftp.target_system = ftpSystem_;
    ftp.target_component = ftpComponent_;
    memcpy(ftp.payload, ftpReply_, sizeof(ftpReply_));
    mavlink_message_t msg;
    ENCODE(file_transfer_protocol, &msg, &ftp);
    return sendMessage(&msg);
}

void MavParamServer::tick(uint32_t now)
{
    if (send_ == nullptr)
        return;

    if (setReplyPending_ && timeReached(now, setReplyAt_))
    {
        setReplyPending_ = false;
        paramMask_ |= 1UL << setIndex_;
        for (uint8_t i = 0; i < count_; i++)
        {
            if (table_[listed_[i]].get() != snapshot_[i])
                paramMask_ |= 1UL << i;
        }
    }

    const uint32_t heartbeatInterval = config_.heartbeatIntervalMs ? config_.heartbeatIntervalMs : DEFAULT_HEARTBEAT_INTERVAL_MS;
    if (!heartbeatStarted_ && (vehicleSysid_ != 0 || timeReached(now, startMs_ + config_.heartbeatWaitMs)))
    {
        heartbeatStarted_ = true;
        heartbeatDue_ = true;
    }

    uint8_t budget = config_.maxFramesPerTick ? config_.maxFramesPerTick : 1;
    // Replies first, they are what a GCS is waiting on
    if (budget && ftpPending_ && sendFtpReply())
    {
        ftpPending_ = false;
        budget--;
    }
    if (budget && ackPending_ && sendCommandAck())
    {
        ackPending_ = false;
        budget--;
    }
    if (budget && versionPending_ && sendAutopilotVersion())
    {
        versionPending_ = false;
        budget--;
    }
    if (budget && statusTextPending_ && sendStatusText())
    {
        statusTextPending_ = false;
        budget--;
    }
    if (budget && heartbeatStarted_ && (heartbeatDue_ || now - lastHeartbeatMs_ >= heartbeatInterval) && sendHeartbeat())
    {
        lastHeartbeatMs_ = now;
        heartbeatDue_ = false;
        budget--;
    }
    for (uint8_t i = 0; i < count_ && budget && paramMask_ != 0; i++)
    {
        if ((paramMask_ & (1UL << i)) == 0)
            continue;
        if (!sendParamValue(i))
            break;
        paramMask_ &= ~(1UL << i);
        budget--;
    }
}
