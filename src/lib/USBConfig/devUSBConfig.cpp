#include "devUSBConfig.h"

#if defined(PLATFORM_STM32)

#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "options.h"
#include "logging.h"
#include "config_json.h"
#include "usbcfg_framing.h"
#include "USBConfigConnector.h"
#include "CRSFRouter.h"

#include "USBVendorStream.h"
#include "stm32_dfu.h"

#if defined(TITAN_UNIFIED_STM32)
#include "elrs_eeprom.h"
#include "hardware_layout.h"
#include "hardware_override_stm32.h"
#endif

#if defined(TARGET_TX)
#include "handset.h"
#endif

extern unsigned long rebootTime;
extern unsigned long dfuRequestTime;

// Session drops if no valid frame arrives inside this window. The host sends TLRS_PING at 1 Hz
// while idle, so this only fires on a yanked cable / crashed host.
#define USBCFG_SESSION_TIMEOUT_MS 3000

// Largest document we will accept on a SET. The TX models export (64 models) is ~8-10 KB.
#define USBCFG_SET_MAX_BYTES 24576

static Stream *s_port = nullptr;
static UsbCfgParser s_parser;
static bool s_sessionActive = false;
static uint32_t s_lastFrameMs = 0;

// SET reassembly state
static char *s_setBuf = nullptr;
static uint32_t s_setLen = 0;
static uint32_t s_setExpected = 0;
static uint16_t s_setNextSeq = 0;
static uint8_t s_setResource = 0;
static bool s_setActive = false;

// CRSF parameter tunnel — only attached to the router while a session is open.
static USBConfigConnector s_crsfConnector;
static bool s_crsfAttached = false;

static void setAbort()
{
    free(s_setBuf);
    s_setBuf = nullptr;
    s_setLen = 0;
    s_setExpected = 0;
    s_setNextSeq = 0;
    s_setActive = false;
}

bool USBConfig_SessionActive()
{
    return s_sessionActive;
}

// Mirrors the LUA posture: refuse writes while the module is armed. There is no armed
// indication on the RX side (and RX config commits already happen over LUA while connected),
// so the guard is TX-only.
static bool moduleIsBusy()
{
#if defined(TARGET_TX)
    return handset != nullptr && handset->IsArmed();
#else
    return false;
#endif
}

// The hardware layout can be overridden only when the layout names a config flash that answered
// (the override is stored there).
static bool hardwareWritable()
{
#if defined(TITAN_UNIFIED_STM32)
    return elrs_ConfigFlash() != nullptr;
#else
    return false;
#endif
}

// Without a hardware layout (connectionState hardwareUndefined) setup() stops before the config
// is loaded and given its storage, so only the options, the layout, reboot and DFU are served —
// enough to fix the layout or reflash the board.
static bool configLoaded()
{
    return connectionState != hardwareUndefined;
}

// The parameter tree is only served once the hardware layout has loaded: without one, setup()
// stops before any CRSF endpoint is registered, so the tunnel would have nothing to reach.
static void attachCrsfTunnel()
{
    if (s_crsfAttached || !configLoaded()) return;
    s_crsfConnector.setPort(s_port);
    crsfRouter.addConnector(&s_crsfConnector);
    s_crsfAttached = true;
}

static void detachCrsfTunnel()
{
    if (!s_crsfAttached) return;
    crsfRouter.removeConnector(&s_crsfConnector);
    s_crsfConnector.setPort(nullptr);
    s_crsfAttached = false;
}

static void closeSession()
{
    s_sessionActive = false;
    // The router must stop writing parameter responses the moment the host goes away, or it keeps
    // filling a pipe nobody is reading.
    detachCrsfTunnel();
    setAbort();
    // Drop any half-parsed frame with the session. Without this a truncated frame leaves the
    // parser mid-payload, where it swallows whatever arrives next — so the first command of the
    // following session is eaten too, and the device looks dead for longer than it is.
    s_parser.reset();
}

// ---------------------------------------------------------------------------------------
// HELLO
// ---------------------------------------------------------------------------------------
static void handleHello()
{
    JsonDocument doc;
    doc["version"] = version;
    doc["git-commit"] = commit;
    doc["target"] = &target_name[4];
    doc["product-name"] = product_name;
#if defined(TARGET_TX)
    doc["module-type"] = "TX";
#else
    doc["module-type"] = "RX";
#endif
#if defined(RADIO_LR1121)
    doc["radio-type"] = "LR1121";
#elif defined(RADIO_LR2021)
    doc["radio-type"] = "LR2021";
#elif defined(RADIO_SX128X)
    doc["radio-type"] = "SX128X";
#elif defined(RADIO_SX127X)
    doc["radio-type"] = "SX127X";
#else
    doc["radio-type"] = "UNKNOWN";
#endif

    // Options are runtime-writable and persisted (lib/OPTIONS/options_storage_stm32.h), which is
    // what lights up the save buttons on the dashboard's binding and options panels.
    uint32_t features = TLRS_FEATURE_OPTIONS_WRITE;
#if defined(STM32_DFU_SUPPORTED)
    features |= TLRS_FEATURE_DFU;
#endif
    if (hardwareWritable())
    {
        features |= TLRS_FEATURE_HARDWARE_WRITE;
    }
    // The tunnel is just a connector on the global router, which both mains build. It is what
    // lights up the dashboard's Parameters tab.
    if (configLoaded())
    {
        features |= TLRS_FEATURE_CRSF_PARAMS;
    }

    uint8_t payload[USBCFG_PAYLOAD_MAX];
    payload[0] = USBCFG_PROTOCOL_VERSION;
    payload[1] = (uint8_t)(features & 0xFF);
    payload[2] = (uint8_t)((features >> 8) & 0xFF);
    payload[3] = (uint8_t)((features >> 16) & 0xFF);
    payload[4] = (uint8_t)((features >> 24) & 0xFF);
    const size_t json = serializeJson(doc, (char *)&payload[5], sizeof(payload) - 5);

    usbcfg_writeFrame(s_port, '>', TLRS_HELLO, payload, (uint16_t)(5 + json));
}

// ---------------------------------------------------------------------------------------
// GET
// ---------------------------------------------------------------------------------------
static void handleGet(const uint8_t *p, const uint16_t len)
{
    if (len < 1)
    {
        usbcfg_writeError(s_port, TLRS_GET, TLRS_ERR_BAD_REQUEST, "missing resource");
        return;
    }
    const uint8_t resource = p[0];
    const uint8_t flags = len > 1 ? p[1] : 0;

    if (resource == TLRS_RES_CONFIG && !configLoaded())
    {
        usbcfg_writeError(s_port, TLRS_GET, TLRS_ERR_UNSUPPORTED, "no hardware layout");
        return;
    }

    JsonDocument doc;
    switch (resource)
    {
    case TLRS_RES_CONFIG:
        ConfigJson_BuildConfig(doc.to<JsonObject>(), (flags & TLRS_GETFLAG_EXPORT) != 0);
        break;
    case TLRS_RES_OPTIONS:
        ConfigJson_BuildOptions(doc.to<JsonObject>());
        break;
#if defined(TITAN_UNIFIED_STM32)
    case TLRS_RES_HARDWARE:
        // The effective layout: the flashed one, or the override (with "customised": true).
        // An empty object when the board was flashed without a layout.
        if (deserializeJson(doc, getHardware()) || !doc.is<JsonObject>())
        {
            doc.to<JsonObject>();
        }
        break;
#endif
    default:
        usbcfg_writeError(s_port, TLRS_GET, TLRS_ERR_UNSUPPORTED, "unknown resource");
        return;
    }

    // Serialize straight onto the wire — measureJson() gives the host the total up front so it
    // can size its reassembly buffer without us ever holding the document as text.
    UsbCfgChunkWriter writer(s_port, TLRS_GET, (uint32_t)measureJson(doc));
    serializeJson(doc, writer);
    writer.finish();
}

// ---------------------------------------------------------------------------------------
// SET
// ---------------------------------------------------------------------------------------
static void setFinalResponse(const uint8_t status, const char *message)
{
    uint8_t buf[128];
    buf[0] = status;
    const size_t len = message ? strnlen(message, sizeof(buf) - 1) : 0;
    if (len) memcpy(&buf[1], message, len);
    usbcfg_writeFrame(s_port, status == 0 ? '>' : '!', TLRS_SET, buf, (uint16_t)(len + 1));
}

#if defined(TITAN_UNIFIED_STM32)
// Mirrors the ESP `POST /hardware.json` (devWIFI.cpp): the document replaces the flashed layout
// from the next boot, nothing is applied live. `config_flash_*` always come from the flashed
// layout (hardware_ApplyOverride), so they are not stored.
static void setHardware(JsonDocument &doc)
{
    if (!doc.is<JsonObject>())
    {
        setFinalResponse(TLRS_ERR_PARSE, "layout must be a JSON object");
        return;
    }
    doc.remove("config_flash_cs");
    doc.remove("config_flash_sck");
    doc.remove("config_flash_miso");
    doc.remove("config_flash_mosi");
    doc["customised"] = true;

    const size_t len = measureJson(doc);
    if (len > ELRSOPTS_HARDWARE_SIZE)
    {
        setFinalResponse(TLRS_ERR_TOO_LARGE, "layout too large");
        return;
    }
    char *json = (char *)malloc(len + 1);
    if (json == nullptr)
    {
        setFinalResponse(TLRS_ERR_INTERNAL, "out of memory");
        return;
    }
    serializeJson(doc, json, len + 1);
    const bool saved = hwOverride_Save(json, len);
    free(json);
    if (!saved)
    {
        setFinalResponse(TLRS_ERR_INTERNAL, "config flash write failed");
        return;
    }
    setFinalResponse(0, "Hardware updated - reboot to apply");
}
#endif

static void handleSet(const uint8_t *p, const uint16_t len)
{
    // resource(1) seq(2) flags(1) [totalLen(4)] json...
    if (len < 4)
    {
        setAbort();
        usbcfg_writeError(s_port, TLRS_SET, TLRS_ERR_BAD_REQUEST, "short chunk");
        return;
    }
    const uint8_t resource = p[0];
    const uint16_t seq = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
    const uint8_t flags = p[3];
    uint16_t offset = 4;

    if (resource != TLRS_RES_CONFIG && resource != TLRS_RES_OPTIONS &&
        !(resource == TLRS_RES_HARDWARE && hardwareWritable()))
    {
        setAbort();
        usbcfg_writeError(s_port, TLRS_SET, TLRS_ERR_UNSUPPORTED, "unknown resource");
        return;
    }
    if (resource == TLRS_RES_CONFIG && !configLoaded())
    {
        setAbort();
        usbcfg_writeError(s_port, TLRS_SET, TLRS_ERR_UNSUPPORTED, "no hardware layout");
        return;
    }

    if (moduleIsBusy())
    {
        setAbort();
        usbcfg_writeError(s_port, TLRS_SET, TLRS_ERR_BUSY, "module is armed");
        return;
    }

    if (flags & TLRS_CHUNK_FIRST)
    {
        setAbort();
        if (len < offset + 4)
        {
            usbcfg_writeError(s_port, TLRS_SET, TLRS_ERR_BAD_REQUEST, "missing total length");
            return;
        }
        s_setExpected = (uint32_t)p[offset] | ((uint32_t)p[offset + 1] << 8) |
                        ((uint32_t)p[offset + 2] << 16) | ((uint32_t)p[offset + 3] << 24);
        offset += 4;
        if (s_setExpected > USBCFG_SET_MAX_BYTES ||
            (resource == TLRS_RES_HARDWARE && s_setExpected > ELRSOPTS_HARDWARE_SIZE))
        {
            usbcfg_writeError(s_port, TLRS_SET, TLRS_ERR_TOO_LARGE, "document too large");
            return;
        }
        s_setBuf = (char *)malloc(s_setExpected + 1);
        if (s_setBuf == nullptr)
        {
            usbcfg_writeError(s_port, TLRS_SET, TLRS_ERR_INTERNAL, "out of memory");
            return;
        }
        s_setResource = resource;
        s_setActive = true;
        s_setNextSeq = 0;
    }

    if (!s_setActive)
    {
        usbcfg_writeError(s_port, TLRS_SET, TLRS_ERR_BAD_REQUEST, "no chunk sequence in progress");
        return;
    }
    if (resource != s_setResource)
    {
        setAbort();
        usbcfg_writeError(s_port, TLRS_SET, TLRS_ERR_BAD_REQUEST, "resource changed mid-sequence");
        return;
    }
    if (seq != s_setNextSeq)
    {
        setAbort();
        usbcfg_writeError(s_port, TLRS_SET, TLRS_ERR_BAD_REQUEST, "chunk out of sequence");
        return;
    }

    const uint16_t dataLen = len - offset;
    if (s_setLen + dataLen > s_setExpected)
    {
        setAbort();
        usbcfg_writeError(s_port, TLRS_SET, TLRS_ERR_TOO_LARGE, "more data than declared");
        return;
    }
    memcpy(s_setBuf + s_setLen, p + offset, dataLen);
    s_setLen += dataLen;
    s_setNextSeq++;

    if (!(flags & TLRS_CHUNK_LAST))
    {
        // Per-chunk ack so the host can flow-control.
        const uint8_t ack[2] = {(uint8_t)(seq & 0xFF), (uint8_t)(seq >> 8)};
        usbcfg_writeFrame(s_port, '>', TLRS_SET, ack, sizeof(ack));
        return;
    }

    s_setBuf[s_setLen] = '\0';

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, s_setBuf, s_setLen);
    if (err)
    {
        setAbort();
        setFinalResponse(TLRS_ERR_PARSE, err.c_str());
        return;
    }
    // The document is parsed; the raw text is no longer needed while we commit to flash.
    free(s_setBuf);
    s_setBuf = nullptr;
    s_setLen = 0;
    s_setActive = false;

#if defined(TITAN_UNIFIED_STM32)
    if (s_setResource == TLRS_RES_HARDWARE)
    {
        setHardware(doc);
        return;
    }
#endif

    // Options are all reboot-to-apply, but we never reboot ourselves — the host drives that with
    // TLRS_REBOOT once the user accepts the prompt.
    const char *applyErr = s_setResource == TLRS_RES_OPTIONS
                               ? ConfigJson_ApplyOptions(doc.as<JsonVariant>())
                               : ConfigJson_ApplyConfig(doc.as<JsonVariant>());
    if (applyErr)
    {
        setFinalResponse(TLRS_ERR_INTERNAL, applyErr);
        return;
    }
    setFinalResponse(0, s_setResource == TLRS_RES_OPTIONS ? "Options updated - reboot to apply"
                                                          : "Configuration updated");
}

// ---------------------------------------------------------------------------------------
static void handleReset(const uint8_t *p, const uint16_t len)
{
    if (moduleIsBusy())
    {
        usbcfg_writeError(s_port, TLRS_RESET, TLRS_ERR_BUSY, "module is armed");
        return;
    }
    const uint8_t flags = len ? p[0] : TLRS_RESETFLAG_CONFIG;
    if ((flags & (TLRS_RESETFLAG_CONFIG | TLRS_RESETFLAG_OPTIONS)) && !configLoaded())
    {
        usbcfg_writeError(s_port, TLRS_RESET, TLRS_ERR_UNSUPPORTED, "no hardware layout");
        return;
    }
#if defined(TITAN_UNIFIED_STM32)
    if (flags & TLRS_RESETFLAG_HARDWARE)
    {
        hwOverride_Clear();
    }
#endif
    ConfigJson_Reset((flags & TLRS_RESETFLAG_CONFIG) != 0, (flags & TLRS_RESETFLAG_OPTIONS) != 0);
    usbcfg_writeAck(s_port, TLRS_RESET);
    s_port->flush();
    rebootTime = millis() + 100;
}

// ---------------------------------------------------------------------------------------
// CRSF tunnel
// ---------------------------------------------------------------------------------------
static void handleCrsf(const uint8_t *p, const uint16_t len)
{
    // Deliberately NOT guarded by moduleIsBusy(). These are the same live link parameters the
    // handset LUA already changes in flight, reached through the same endpoint — gating one
    // transport and not the other would make the web UI less capable than the handset for no
    // safety gain. The static-config paths (SET/RESET/REBOOT) keep their armed guard because they
    // need a reboot to apply. Parameters that genuinely must not move while armed are marked as
    // such in the parameter definitions, which is the right place for it.
    if (!s_crsfAttached) return;
    if (len < CRSF_MIN_PACKET_LEN) return;

    const uint8_t frameSize = p[1];
    // frame_size counts everything after itself: type .. crc inclusive.
    if (frameSize < 2 || (uint16_t)frameSize + CRSF_FRAME_NOT_COUNTED_BYTES != len) return;
    if (len > CRSF_FRAME_SIZE_MAX) return;

    if (crsfRouter.crsf_crc.calc(&p[2], frameSize - 1, 0) != p[len - 1]) return;

    // Invalid frames are dropped without a reply: this is a stream of somebody else's protocol,
    // and a '!' here would be read as a response to whatever request is actually in flight.
    crsfRouter.processMessage(&s_crsfConnector, (const crsf_header_t *)p);
}

static void handleFrame(const uint16_t function, const uint8_t *p, const uint16_t len)
{
    if (!s_sessionActive)
    {
        // A session only opens on HELLO. Any other frame arriving first is from a host that lost
        // track of the session state, so ignore it rather than half-opening.
        if (function != TLRS_HELLO) return;
        s_sessionActive = true;
        attachCrsfTunnel();
        DBGLN("USBConfig: session opened");
    }

    s_lastFrameMs = millis();

    switch (function)
    {
    case TLRS_HELLO:
        handleHello();
        break;

    case TLRS_PING:
        usbcfg_writeAck(s_port, TLRS_PING);
        break;

    case TLRS_BYE:
        usbcfg_writeAck(s_port, TLRS_BYE);
        // Deliberately no flush(): the host has just told us it is going away and tears the
        // interface down the moment it sees this ack, so waiting for the pipe to drain is at
        // best a no-op and at worst a stall on a host that is already gone. The reboot paths
        // below do need the ack out first, and their flush is bounded.
        closeSession();
        DBGLN("USBConfig: session closed");
        break;

    case TLRS_CRSF:
        handleCrsf(p, len);
        break;

    case TLRS_GET:
        handleGet(p, len);
        break;

    case TLRS_SET:
        handleSet(p, len);
        break;

    case TLRS_REBOOT:
        if (moduleIsBusy())
        {
            usbcfg_writeError(s_port, TLRS_REBOOT, TLRS_ERR_BUSY, "module is armed");
            break;
        }
        usbcfg_writeAck(s_port, TLRS_REBOOT);
        s_port->flush();
        rebootTime = millis() + 100;
        break;

    case TLRS_RESET:
        handleReset(p, len);
        break;

    case TLRS_DFU:
#if defined(STM32_DFU_SUPPORTED)
        if (moduleIsBusy())
        {
            usbcfg_writeError(s_port, TLRS_DFU, TLRS_ERR_BUSY, "module is armed");
            break;
        }
        usbcfg_writeAck(s_port, TLRS_DFU);
        s_port->flush();
        dfuRequestTime = millis() + 100;
#else
        usbcfg_writeError(s_port, TLRS_DFU, TLRS_ERR_UNSUPPORTED, "DFU not supported on this MCU");
#endif
        break;

    default:
        usbcfg_writeError(s_port, function, TLRS_ERR_UNSUPPORTED, "unknown function");
        break;
    }
}

bool USBConfig_ProcessBytes(const uint8_t *buf, const uint16_t len)
{
    if (s_port == nullptr) return false;

    const bool wasActive = s_sessionActive;
    for (uint16_t i = 0; i < len; ++i)
    {
        if (s_parser.feed(buf[i]))
        {
            handleFrame(s_parser.function(), s_parser.payload(), s_parser.payloadSize());
        }
    }
    return wasActive || s_sessionActive;
}

// ---------------------------------------------------------------------------------------
// device_t hooks
// ---------------------------------------------------------------------------------------
static bool initialize()
{
    return true;
}

static int start()
{
    // Both TX and RX own the vendor-class config pipe outright (lib/USBComposite). It is not a
    // serial port, so nothing else on the host can enumerate or claim it, and there is no sharing
    // with MAVLink or CRSF to arbitrate.
    SerialCfg.begin();
    s_port = &SerialCfg;
    return 10; // ms
}

void USBConfig_DrainPort()
{
    if (s_port == nullptr) return;

    // Ask readBytes() only for bytes that have already arrived — asking for more blocks until the
    // stream timeout expires.
    uint8_t buf[128];
    for (;;)
    {
        const int avail = s_port->available();
        if (avail <= 0) break;
        const size_t want = (size_t)avail < sizeof(buf) ? (size_t)avail : sizeof(buf);
        const size_t n = s_port->readBytes(buf, want);
        if (n == 0) break;
        USBConfig_ProcessBytes(buf, (uint16_t)n);
    }
}

static int timeout()
{
    if (s_sessionActive && (millis() - s_lastFrameMs) > USBCFG_SESSION_TIMEOUT_MS)
    {
        DBGLN("USBConfig: session timed out");
        closeSession();
    }
    return 10; // ms
}

device_t USBConfig_device = {
    .initialize = initialize,
    .start = start,
    .event = nullptr,
    .timeout = timeout,
    .subscribe = EVENT_NONE
};

#endif /* PLATFORM_STM32 */
