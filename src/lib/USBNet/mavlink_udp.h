#pragma once

#if defined(PLATFORM_STM32)

#include <stdint.h>

/*
 * MAVLink over UDP on the USB network interface, next to (not instead of) the CDC serial port.
 *
 * The device listens on 10.73.x.1:14555 and sends to the host's 14550 — the port a GCS listens on
 * by default (Mission Planner / QGC "UDP" connection). Once a GCS has sent us anything, replies go
 * to wherever that came from instead, so a GCS on a non-default port works too.
 */

#define MAVLINK_UDP_LOCAL_PORT 14555
#define MAVLINK_UDP_GCS_PORT 14550

typedef void (*MavlinkUdpReceiver)(const uint8_t *data, uint16_t len);

/** Open the socket (brings up the network if needed). `receiver` gets every datagram from a GCS. */
void MavlinkUdp_Begin(MavlinkUdpReceiver receiver);

/** Send downlink MAVLink bytes to the GCS. Dropped while the USB network link is down. */
void MavlinkUdp_Send(const uint8_t *data, uint16_t len);

#endif
