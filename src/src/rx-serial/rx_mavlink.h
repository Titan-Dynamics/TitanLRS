#pragma once

#if defined(TARGET_RX)

#include <stdint.h>

#include "FIFO.h"
#include "SerialMavlink.h"

// The RX's own frames only go down the link to the GCS, never to the flight controller

#if !defined(TLRS_MAV_RX_COMPID)
#define TLRS_MAV_RX_COMPID 68 // MAV_COMP_ID_TELEMETRY_RADIO, as the RX's RADIO_STATUS
#endif

struct __mavlink_message;

// fcFrameHeld: bytes of a partly received FC frame that must still fit in the downlink queue
void RxMavlink_Attach(FIFO<MAV_INPUT_BUF_LEN> *downlink, const uint16_t *fcFrameHeld);
void RxMavlink_Detach();
void RxMavlink_HandleVehicle(const __mavlink_message *msg);
void RxMavlink_HandleGcs(const __mavlink_message *msg, uint32_t now);
bool RxMavlink_IsAddressedToRx(const __mavlink_message *msg);
void RxMavlink_DownlinkPopped(uint16_t count);
void RxMavlink_Tick(uint32_t now, bool connected);
// A config commit drops the link, so hold it until the reply to a parameter write has gone out
bool RxMavlink_HoldCommit(uint32_t now, bool downlinkBusy);

#endif // TARGET_RX
