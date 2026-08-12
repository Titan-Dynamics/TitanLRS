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
enum : uint16_t {
    TCFG_HELLO  = 0x5443,   // protoVer(1)                  -> protoVer(1) features(4,LE) json
    TCFG_BYE    = 0x5444,   // -                            -> ack
    TCFG_GET    = 0x5445,   // resource(1) flags(1)         -> chunked JSON
    TCFG_SET    = 0x5446,   // resource(1) chunk...         -> per-chunk ack / final status
    TCFG_REBOOT = 0x5447,   // -                            -> ack, reboot ~100 ms later
    TCFG_RESET  = 0x5448,   // flags(1)                     -> ack, reboot
    TCFG_PING   = 0x5449,   // -                            -> ack (session keepalive)
};

// ---- resources ------------------------------------------------------------------------
enum : uint8_t {
    TCFG_RES_CONFIG  = 0,   // the full /config document: options + config + settings
    TCFG_RES_OPTIONS = 1,   // firmwareOptions; SET requires the OPTIONS_WRITE feature bit
};

// ---- GET request flags ----------------------------------------------------------------
#define TCFG_GETFLAG_EXPORT  (1u << 0)   // mirrors the HTTP `?export` argument

// ---- RESET flags ----------------------------------------------------------------------
#define TCFG_RESETFLAG_CONFIG  (1u << 0) // mirrors `?config` / `?model`
#define TCFG_RESETFLAG_OPTIONS (1u << 1) // mirrors `?options`

// ---- HELLO feature bitmask ------------------------------------------------------------
#define TCFG_FEATURE_OPTIONS_WRITE (1u << 0) // SET(options) supported and persisted
#define TCFG_FEATURE_CW            (1u << 1) // continuous-wave control (Phase 1.3)
#define TCFG_FEATURE_LR1121_UPDATE (1u << 2) // LR1121 firmware upload (Phase 1.3)

// ---- chunk header flags ---------------------------------------------------------------
// Chunk layout (both directions):
//   seq(2,LE) flags(1) [totalLen(4,LE) — first chunk only] json bytes...
#define TCFG_CHUNK_FIRST (1u << 0)
#define TCFG_CHUNK_LAST  (1u << 1)

// ---- error codes (payload byte 0 of a '!' response) -----------------------------------
enum : uint8_t {
    TCFG_ERR_UNSUPPORTED = 1,   // unknown function / resource / not implemented on this build
    TCFG_ERR_BUSY        = 2,   // module armed — writes refused
    TCFG_ERR_BAD_REQUEST = 3,   // malformed payload / chunk sequence
    TCFG_ERR_TOO_LARGE   = 4,   // document exceeds the receive buffer
    TCFG_ERR_PARSE       = 5,   // JSON did not parse
    TCFG_ERR_NO_SESSION  = 6,   // command sent before TCFG_HELLO
    TCFG_ERR_INTERNAL    = 7,   // out of memory / apply failed
};
