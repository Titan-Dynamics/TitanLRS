#pragma once

/*
 * TitanLRS USB Config protocol — wire definitions shared by the firmware and the host tools
 * =========================================================================================
 *
 * Framing is MSPv2-compatible:
 *
 *   host -> device : '$' 'X' '<' flags(1) function(2,LE) size(2,LE) payload[size] crc8_dvb_s2(1)
 *   device -> host : '$' 'X' '>' ...            (success)
 *                    '$' 'X' '!' ...            (error; payload = errCode(1) + ascii message)
 *
 * The CRC covers flags, function, size and payload (standard MSPv2).
 *
 * This is a STANDALONE implementation in lib/USBConfig — lib/MSP is deliberately NOT used and
 * NOT modified, because its MSP_PORT_INBUF_SIZE (64) buffer is shared with the CRSF-encapsulated
 * telemetry path and must not be enlarged.
 *
 * Extensibility contract: an unknown function or resource is answered with '!' + ERR_UNSUPPORTED.
 * Hosts probe, they never assume.
 */

#include <stdint.h>

#define USBCFG_PROTOCOL_VERSION 1

// Max JSON bytes carried in one chunk. Keeps a whole frame comfortably under the 1024-byte
// payload buffer once the chunk header is accounted for.
#define USBCFG_CHUNK_MAX 1000

// ---- function IDs (base 0x5443 = 'TC') ------------------------------------------------
// The identifier prefix is TLRS_; the wire values keep the original 'TC' base.
enum : uint16_t {
    TLRS_HELLO  = 0x5443,   // protoVer(1)                  -> protoVer(1) features(4,LE) json
    TLRS_BYE    = 0x5444,   // -                            -> ack
    TLRS_GET    = 0x5445,   // resource(1) flags(1)         -> chunked JSON
    TLRS_SET    = 0x5446,   // resource(1) chunk...         -> per-chunk ack / final status
    TLRS_REBOOT = 0x5447,   // -                            -> ack, reboot ~100 ms later
    TLRS_RESET  = 0x5448,   // flags(1)                     -> ack, reboot
    TLRS_PING   = 0x5449,   // -                            -> ack (session keepalive)
    TLRS_CRSF   = 0x544A,   // one raw CRSF frame           -> (async) one raw CRSF frame
    TLRS_DFU    = 0x544B,   // -                            -> ack, reset into ROM DFU ~100 ms later
};

// TLRS_CRSF is the one function that breaks the request/response pattern: it is a tunnel for
// somebody else's protocol (the CRSF parameter tree the handset LUA drives), so frames flow in
// both directions unsolicited and are never acknowledged.
//
//  - Payload is exactly ONE CRSF frame, sync byte through CRC inclusive. A CRSF frame is at most
//    64 bytes against a 1017-byte payload, so batching would buy nothing and only complicate the
//    parser. Do not batch.
//  - Inbound frames are validated (length field + CRC-8) and silently DROPPED if invalid. They
//    are deliberately not answered with '!': a stray error frame would desynchronise the
//    request/response pairing every other function depends on.
//  - Outbound frames are '>' with no request outstanding, so a host framing layer must route by
//    function before matching against whatever request is in flight.
//  - The tunnel only exists while a session is open; the connector is registered on HELLO and
//    removed on BYE/timeout.

// ---- resources ------------------------------------------------------------------------
enum : uint8_t {
    TLRS_RES_CONFIG  = 0,   // the full /config document: options + config + settings
    TLRS_RES_OPTIONS = 1,   // firmwareOptions; SET requires the OPTIONS_WRITE feature bit
    TLRS_RES_HARDWARE = 2,  // hardware layout; SET requires the HARDWARE_WRITE feature bit
};

// ---- GET request flags ----------------------------------------------------------------
#define TLRS_GETFLAG_EXPORT  (1u << 0)   // mirrors the HTTP `?export` argument

// ---- RESET flags ----------------------------------------------------------------------
#define TLRS_RESETFLAG_CONFIG  (1u << 0) // mirrors `?config` / `?model`
#define TLRS_RESETFLAG_OPTIONS (1u << 1) // mirrors `?options`
#define TLRS_RESETFLAG_HARDWARE (1u << 2) // mirrors `?hardware`: drops the saved layout override

// ---- HELLO feature bitmask ------------------------------------------------------------
#define TLRS_FEATURE_OPTIONS_WRITE (1u << 0) // SET(options) supported and persisted
#define TLRS_FEATURE_CW            (1u << 1) // continuous-wave control (Phase 1.3)
#define TLRS_FEATURE_LR1121_UPDATE (1u << 2) // LR1121 firmware upload (Phase 1.3)
#define TLRS_FEATURE_CRSF_PARAMS   (1u << 3) // TLRS_CRSF tunnel to the CRSF parameter tree
#define TLRS_FEATURE_DFU            (1u << 4) // TLRS_DFU reboots into the MCU's ROM DFU bootloader
#define TLRS_FEATURE_HARDWARE_WRITE (1u << 5) // SET(hardware) / RESET(hardware) persist a layout override

// ---- chunk header flags ---------------------------------------------------------------
// Chunk layout (both directions):
//   seq(2,LE) flags(1) [totalLen(4,LE) — first chunk only] json bytes...
#define TLRS_CHUNK_FIRST (1u << 0)
#define TLRS_CHUNK_LAST  (1u << 1)

// ---- error codes (payload byte 0 of a '!' response) -----------------------------------
enum : uint8_t {
    TLRS_ERR_UNSUPPORTED = 1,   // unknown function / resource / not implemented on this build
    TLRS_ERR_BUSY        = 2,   // module armed — writes refused
    TLRS_ERR_BAD_REQUEST = 3,   // malformed payload / chunk sequence
    TLRS_ERR_TOO_LARGE   = 4,   // document exceeds the receive buffer
    TLRS_ERR_PARSE       = 5,   // JSON did not parse
    TLRS_ERR_NO_SESSION  = 6,   // command sent before TLRS_HELLO
    TLRS_ERR_INTERNAL    = 7,   // out of memory / apply failed
};
