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

#if defined(TARGET_TX)
#include "handset.h"
extern Stream *TxUSB;
#endif

extern unsigned long rebootTime;

// Session drops if no valid frame arrives inside this window. The host sends TCFG_PING at 1 Hz
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

static void closeSession()
{
    s_sessionActive = false;
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
    const uint32_t features = TCFG_FEATURE_OPTIONS_WRITE;

    uint8_t payload[USBCFG_PAYLOAD_MAX];
    payload[0] = USBCFG_PROTOCOL_VERSION;
    payload[1] = (uint8_t)(features & 0xFF);
    payload[2] = (uint8_t)((features >> 8) & 0xFF);
    payload[3] = (uint8_t)((features >> 16) & 0xFF);
    payload[4] = (uint8_t)((features >> 24) & 0xFF);
    const size_t json = serializeJson(doc, (char *)&payload[5], sizeof(payload) - 5);

    usbcfg_writeFrame(s_port, '>', TCFG_HELLO, payload, (uint16_t)(5 + json));
}

// ---------------------------------------------------------------------------------------
// GET
// ---------------------------------------------------------------------------------------
static void handleGet(const uint8_t *p, const uint16_t len)
{
    if (len < 1)
    {
        usbcfg_writeError(s_port, TCFG_GET, TCFG_ERR_BAD_REQUEST, "missing resource");
        return;
    }
    const uint8_t resource = p[0];
    const uint8_t flags = len > 1 ? p[1] : 0;

    JsonDocument doc;
    switch (resource)
    {
    case TCFG_RES_CONFIG:
        ConfigJson_BuildConfig(doc.to<JsonObject>(), (flags & TCFG_GETFLAG_EXPORT) != 0);
        break;
    case TCFG_RES_OPTIONS:
        ConfigJson_BuildOptions(doc.to<JsonObject>());
        break;
    default:
        usbcfg_writeError(s_port, TCFG_GET, TCFG_ERR_UNSUPPORTED, "unknown resource");
        return;
    }

    // Serialize straight onto the wire — measureJson() gives the host the total up front so it
    // can size its reassembly buffer without us ever holding the document as text.
    UsbCfgChunkWriter writer(s_port, TCFG_GET, (uint32_t)measureJson(doc));
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
    usbcfg_writeFrame(s_port, status == 0 ? '>' : '!', TCFG_SET, buf, (uint16_t)(len + 1));
}

static void handleSet(const uint8_t *p, const uint16_t len)
{
    // resource(1) seq(2) flags(1) [totalLen(4)] json...
    if (len < 4)
    {
        setAbort();
        usbcfg_writeError(s_port, TCFG_SET, TCFG_ERR_BAD_REQUEST, "short chunk");
        return;
    }
    const uint8_t resource = p[0];
    const uint16_t seq = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
    const uint8_t flags = p[3];
    uint16_t offset = 4;

    if (resource != TCFG_RES_CONFIG && resource != TCFG_RES_OPTIONS)
    {
        setAbort();
        usbcfg_writeError(s_port, TCFG_SET, TCFG_ERR_UNSUPPORTED, "unknown resource");
        return;
    }

    if (moduleIsBusy())
    {
        setAbort();
        usbcfg_writeError(s_port, TCFG_SET, TCFG_ERR_BUSY, "module is armed");
        return;
    }

    if (flags & TCFG_CHUNK_FIRST)
    {
        setAbort();
        if (len < offset + 4)
        {
            usbcfg_writeError(s_port, TCFG_SET, TCFG_ERR_BAD_REQUEST, "missing total length");
            return;
        }
        s_setExpected = (uint32_t)p[offset] | ((uint32_t)p[offset + 1] << 8) |
                        ((uint32_t)p[offset + 2] << 16) | ((uint32_t)p[offset + 3] << 24);
        offset += 4;
        if (s_setExpected > USBCFG_SET_MAX_BYTES)
        {
            usbcfg_writeError(s_port, TCFG_SET, TCFG_ERR_TOO_LARGE, "document too large");
            return;
        }
        s_setBuf = (char *)malloc(s_setExpected + 1);
        if (s_setBuf == nullptr)
        {
            usbcfg_writeError(s_port, TCFG_SET, TCFG_ERR_INTERNAL, "out of memory");
            return;
        }
        s_setResource = resource;
        s_setActive = true;
        s_setNextSeq = 0;
    }

    if (!s_setActive)
    {
        usbcfg_writeError(s_port, TCFG_SET, TCFG_ERR_BAD_REQUEST, "no chunk sequence in progress");
        return;
    }
    if (resource != s_setResource)
    {
        setAbort();
        usbcfg_writeError(s_port, TCFG_SET, TCFG_ERR_BAD_REQUEST, "resource changed mid-sequence");
        return;
    }
    if (seq != s_setNextSeq)
    {
        setAbort();
        usbcfg_writeError(s_port, TCFG_SET, TCFG_ERR_BAD_REQUEST, "chunk out of sequence");
        return;
    }

    const uint16_t dataLen = len - offset;
    if (s_setLen + dataLen > s_setExpected)
    {
        setAbort();
        usbcfg_writeError(s_port, TCFG_SET, TCFG_ERR_TOO_LARGE, "more data than declared");
        return;
    }
    memcpy(s_setBuf + s_setLen, p + offset, dataLen);
    s_setLen += dataLen;
    s_setNextSeq++;

    if (!(flags & TCFG_CHUNK_LAST))
    {
        // Per-chunk ack so the host can flow-control.
        const uint8_t ack[2] = {(uint8_t)(seq & 0xFF), (uint8_t)(seq >> 8)};
        usbcfg_writeFrame(s_port, '>', TCFG_SET, ack, sizeof(ack));
        return;
    }

    s_setBuf[s_setLen] = '\0';

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, s_setBuf, s_setLen);
    if (err)
    {
        setAbort();
        setFinalResponse(TCFG_ERR_PARSE, err.c_str());
        return;
    }
    // The document is parsed; the raw text is no longer needed while we commit to flash.
    free(s_setBuf);
    s_setBuf = nullptr;
    s_setLen = 0;
    s_setActive = false;

    // Options are all reboot-to-apply, but we never reboot ourselves — the host drives that with
    // TCFG_REBOOT once the user accepts the prompt.
    const char *applyErr = s_setResource == TCFG_RES_OPTIONS
                               ? ConfigJson_ApplyOptions(doc.as<JsonVariant>())
                               : ConfigJson_ApplyConfig(doc.as<JsonVariant>());
    if (applyErr)
    {
        setFinalResponse(TCFG_ERR_INTERNAL, applyErr);
        return;
    }
    setFinalResponse(0, s_setResource == TCFG_RES_OPTIONS ? "Options updated - reboot to apply"
                                                          : "Configuration updated");
}

// ---------------------------------------------------------------------------------------
static void handleReset(const uint8_t *p, const uint16_t len)
{
    if (moduleIsBusy())
    {
        usbcfg_writeError(s_port, TCFG_RESET, TCFG_ERR_BUSY, "module is armed");
        return;
    }
    const uint8_t flags = len ? p[0] : TCFG_RESETFLAG_CONFIG;
    ConfigJson_Reset((flags & TCFG_RESETFLAG_CONFIG) != 0, (flags & TCFG_RESETFLAG_OPTIONS) != 0);
    usbcfg_writeAck(s_port, TCFG_RESET);
    s_port->flush();
    rebootTime = millis() + 100;
}

static void handleFrame(const uint16_t function, const uint8_t *p, const uint16_t len)
{
    if (!s_sessionActive)
    {
        // Sniff mode: only HELLO is acted upon. Anything else on the wire belongs to whoever
        // else is using this port (MAVLink / CRSF) and must be left alone.
        if (function != TCFG_HELLO) return;
        s_sessionActive = true;
        DBGLN("USBConfig: session opened");
    }

    s_lastFrameMs = millis();

    switch (function)
    {
    case TCFG_HELLO:
        handleHello();
        break;

    case TCFG_PING:
        usbcfg_writeAck(s_port, TCFG_PING);
        break;

    case TCFG_BYE:
        usbcfg_writeAck(s_port, TCFG_BYE);
        s_port->flush();
        closeSession();
        DBGLN("USBConfig: session closed");
        break;

    case TCFG_GET:
        handleGet(p, len);
        break;

    case TCFG_SET:
        handleSet(p, len);
        break;

    case TCFG_REBOOT:
        if (moduleIsBusy())
        {
            usbcfg_writeError(s_port, TCFG_REBOOT, TCFG_ERR_BUSY, "module is armed");
            break;
        }
        usbcfg_writeAck(s_port, TCFG_REBOOT);
        s_port->flush();
        rebootTime = millis() + 100;
        break;

    case TCFG_RESET:
        handleReset(p, len);
        break;

    default:
        usbcfg_writeError(s_port, function, TCFG_ERR_UNSUPPORTED, "unknown function");
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
#if defined(TARGET_TX)
    // The TX shares the CDC port with MAVLink; tx_main owns it and feeds us via
    // USBConfig_ProcessBytes(). We only need the handle for responses.
    s_port = TxUSB;
#else
    // The RX does not use the CDC port for anything else, so the config service owns it.
    Serial.begin(460800);
    Serial.dtr(false);   // STM32duino drops TX bytes unless DTR is asserted by the host
    s_port = &Serial;
#endif
    return 10; // ms
}

static int timeout()
{
#if defined(TARGET_RX)
    // RX drains the port itself; TX is fed from HandleUARTin().
    //
    // Read only what has actually arrived. Stream::readBytes() blocks until it has filled the
    // buffer it was given or the stream timeout expires (1 s by default on STM32duino), so asking
    // for a fixed 128 bytes stalled here for a full second on every frame shorter than that —
    // which is every HELLO / PING / GET / REBOOT and the last chunk of every SET. That put ~1 s
    // into each round trip, pushed saves past the host's 2 s request timeout ("Device did not
    // respond"), and blocked the RX main loop while it waited. tx_main has always read
    // min(free, available()), which is why the TX side was unaffected.
    // Ask readBytes() only for bytes that have already arrived, exactly as tx_main does. Reading
    // byte-at-a-time instead drives CDC_resume_receive() once per byte from the main loop while
    // the USB ISR calls it too; they share the receive queue's wrap bookkeeping, which is only
    // exercised once more than ~128 bytes (two USB packets) have flowed — and that is precisely
    // where inbound frames started disappearing. One block read per drain, one resume.
    if (s_port != nullptr)
    {
        uint8_t buf[128];
        for (;;)
        {
            const int avail = Serial.available();
            if (avail <= 0) break;
            const size_t want = (size_t)avail < sizeof(buf) ? (size_t)avail : sizeof(buf);
            const size_t n = Serial.readBytes(buf, want);
            if (n == 0) break;
            USBConfig_ProcessBytes(buf, (uint16_t)n);
        }
    }
#endif

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
