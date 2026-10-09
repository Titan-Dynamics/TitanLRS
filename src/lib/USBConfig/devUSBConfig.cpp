#include "devUSBConfig.h"

#if defined(PLATFORM_STM32)

#include <ArduinoJson.h>
#include <string.h>

#include "common.h"
#include "options.h"
#include "logging.h"
#include "config_json.h"
#include "usbcfg_api.h"
#include "USBConfigConnector.h"
#include "CRSFRouter.h"

#include "usbnet.h"
#include "http_server.h"
#include "stm32_dfu.h"

#include "elrs_eeprom.h"
#include "hardware_layout.h"
#include "hardware_override_stm32.h"

#if defined(TARGET_TX)
#include "handset.h"
#endif

extern unsigned long rebootTime;
extern unsigned long dfuRequestTime;

// The CRSF tunnel is dropped from the router once the browser has not polled for this long.
#define CRSF_TUNNEL_IDLE_MS 3000
// Longest a GET /crsf may wait for frames before answering 204.
#define CRSF_POLL_MAX_WAIT_MS 2000
#define CRSF_POLL_DEFAULT_WAIT_MS 1000
// Most bytes of queued frames returned by one poll.
#define CRSF_POLL_MAX_BYTES 1024

// CRSF parameter tunnel — only attached to the router while the browser is polling it.
static USBConfigConnector s_crsfConnector;
static bool s_crsfAttached = false;
static uint32_t s_crsfLastRequestMs = 0;
static HttpDeferred s_crsfPoll = {nullptr, 0};
static uint32_t s_crsfPollDeadline = 0;

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
    return elrs_ConfigFlash() != nullptr;
}

// Without a hardware layout (connectionState hardwareUndefined) setup() stops before the config
// is loaded and given its storage, so only the options, the layout, reboot and DFU are served —
// enough to fix the layout or reflash the board.
static bool configLoaded()
{
    return connectionState != hardwareUndefined;
}

// ---------------------------------------------------------------------------------------
// responses
// ---------------------------------------------------------------------------------------
static void sendJson(HttpResponse &res, JsonDocument &doc, int status = 200)
{
    const size_t len = measureJson(doc);
    char *body = res.allocBody(len);
    if (body == nullptr)
    {
        res.send(500, "application/json", "{\"error\":\"out of memory\"}");
        return;
    }
    serializeJson(doc, body, len + 1);
    res.send(status, "application/json");
}

static void sendError(HttpResponse &res, int status, const char *message)
{
    JsonDocument doc;
    doc["error"] = message;
    sendJson(res, doc, status);
}

static void sendStatus(HttpResponse &res, const char *message)
{
    JsonDocument doc;
    doc["status"] = message;
    sendJson(res, doc);
}

/** Parses the request body as JSON; answers 400 and returns false if it does not parse. */
static bool parseBody(const HttpRequest &req, HttpResponse &res, JsonDocument &doc)
{
    const DeserializationError err = deserializeJson(doc, (const char *)req.body, req.bodyLen);
    if (err)
    {
        sendError(res, 400, err.c_str());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------
// /hello
// ---------------------------------------------------------------------------------------
static void handleHello(HttpResponse &res)
{
    JsonDocument doc;
    doc["api-version"] = USBCFG_API_VERSION;
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
    doc["has-hardware"] = configLoaded();

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
    // The parameter tree is only served once the hardware layout has loaded: without one,
    // setup() stops before any CRSF endpoint is registered, so the tunnel would reach nothing.
    if (configLoaded())
    {
        features |= TLRS_FEATURE_CRSF_PARAMS;
    }
    doc["features"] = features;

    sendJson(res, doc);
}

// ---------------------------------------------------------------------------------------
// /config, /import, /options.json
// ---------------------------------------------------------------------------------------
static void handleGetConfig(const HttpRequest &req, HttpResponse &res)
{
    if (!configLoaded())
    {
        sendError(res, 404, "no hardware layout");
        return;
    }
    char value[8];
    const bool exportMode = Http_QueryParam(req.query, "export", value, sizeof(value)) != nullptr;
    JsonDocument doc;
    ConfigJson_BuildConfig(doc.to<JsonObject>(), exportMode);
    sendJson(res, doc);
}

static void handleSetConfig(const HttpRequest &req, HttpResponse &res)
{
    if (!configLoaded())
    {
        sendError(res, 404, "no hardware layout");
        return;
    }
    if (moduleIsBusy())
    {
        sendError(res, 409, "module is armed");
        return;
    }
    JsonDocument doc;
    if (!parseBody(req, res, doc))
    {
        return;
    }
    const char *err = ConfigJson_ApplyConfig(doc.as<JsonVariant>());
    if (err)
    {
        sendError(res, 500, err);
        return;
    }
    sendStatus(res, "Configuration updated");
}

static void handleGetOptions(HttpResponse &res)
{
    JsonDocument doc;
    ConfigJson_BuildOptions(doc.to<JsonObject>());
    sendJson(res, doc);
}

static void handleSetOptions(const HttpRequest &req, HttpResponse &res)
{
    if (moduleIsBusy())
    {
        sendError(res, 409, "module is armed");
        return;
    }
    JsonDocument doc;
    if (!parseBody(req, res, doc))
    {
        return;
    }
    // Options are all reboot-to-apply, but we never reboot ourselves — the client drives that
    // with POST /reboot once the user accepts the prompt.
    const char *err = ConfigJson_ApplyOptions(doc.as<JsonVariant>());
    if (err)
    {
        sendError(res, 500, err);
        return;
    }
    sendStatus(res, "Options updated - reboot to apply");
}

// ---------------------------------------------------------------------------------------
// /hardware.json
// ---------------------------------------------------------------------------------------
static void handleGetHardware(HttpResponse &res)
{
    // The effective layout: the flashed one, or the override (with "customised": true). An
    // empty object when the board was flashed without a layout.
    JsonDocument doc;
    if (deserializeJson(doc, getHardware()) || !doc.is<JsonObject>())
    {
        doc.to<JsonObject>();
    }
    sendJson(res, doc);
}

static void handleSetHardware(const HttpRequest &req, HttpResponse &res)
{
    if (!hardwareWritable())
    {
        sendError(res, 404, "no config flash to store a layout in");
        return;
    }
    if (moduleIsBusy())
    {
        sendError(res, 409, "module is armed");
        return;
    }
    if (req.bodyLen > ELRSOPTS_HARDWARE_SIZE)
    {
        sendError(res, 413, "layout too large");
        return;
    }
    JsonDocument doc;
    if (!parseBody(req, res, doc))
    {
        return;
    }
    if (!doc.is<JsonObject>())
    {
        sendError(res, 400, "layout must be a JSON object");
        return;
    }
    // Mirrors the ESP `POST /hardware.json` (devWIFI.cpp): the document replaces the flashed
    // layout from the next boot, nothing is applied live. `config_flash_*` always come from the
    // flashed layout (hardware_ApplyOverride), so they are not stored.
    doc.remove("config_flash_cs");
    doc.remove("config_flash_sck");
    doc.remove("config_flash_miso");
    doc.remove("config_flash_mosi");
    doc["customised"] = true;

    const size_t len = measureJson(doc);
    if (len > ELRSOPTS_HARDWARE_SIZE)
    {
        sendError(res, 413, "layout too large");
        return;
    }
    char *json = (char *)malloc(len + 1);
    if (json == nullptr)
    {
        sendError(res, 500, "out of memory");
        return;
    }
    serializeJson(doc, json, len + 1);
    const bool saved = hwOverride_Save(json, len);
    free(json);
    if (!saved)
    {
        sendError(res, 500, "config flash write failed");
        return;
    }
    sendStatus(res, "Hardware updated - reboot to apply");
}

// ---------------------------------------------------------------------------------------
// /reboot, /reset, /dfu
// ---------------------------------------------------------------------------------------
static void handleReboot(HttpResponse &res)
{
    if (moduleIsBusy())
    {
        sendError(res, 409, "module is armed");
        return;
    }
    sendStatus(res, "Rebooting");
    rebootTime = millis() + 100;
}

static void handleReset(const HttpRequest &req, HttpResponse &res)
{
    if (moduleIsBusy())
    {
        sendError(res, 409, "module is armed");
        return;
    }
    char value[8];
    const bool resetConfig = Http_QueryParam(req.query, "config", value, sizeof(value)) != nullptr ||
                             Http_QueryParam(req.query, "model", value, sizeof(value)) != nullptr;
    const bool resetOptions = Http_QueryParam(req.query, "options", value, sizeof(value)) != nullptr;
    const bool resetHardware = Http_QueryParam(req.query, "hardware", value, sizeof(value)) != nullptr;
    if ((resetConfig || resetOptions) && !configLoaded())
    {
        sendError(res, 404, "no hardware layout");
        return;
    }
    if (resetHardware)
    {
        hwOverride_Clear();
    }
    ConfigJson_Reset(resetConfig, resetOptions);
    sendStatus(res, "Reset - rebooting");
    rebootTime = millis() + 100;
}

static void handleDfu(HttpResponse &res)
{
#if defined(STM32_DFU_SUPPORTED)
    if (moduleIsBusy())
    {
        sendError(res, 409, "module is armed");
        return;
    }
    sendStatus(res, "Rebooting into DFU");
    dfuRequestTime = millis() + 100;
#else
    sendError(res, 404, "DFU not supported on this MCU");
#endif
}

// ---------------------------------------------------------------------------------------
// CRSF tunnel
// ---------------------------------------------------------------------------------------
static void attachCrsfTunnel()
{
    s_crsfLastRequestMs = millis();
    if (s_crsfAttached || !configLoaded())
    {
        return;
    }
    s_crsfConnector.clear();
    crsfRouter.addConnector(&s_crsfConnector);
    s_crsfAttached = true;
    DBGLN("USBConfig: CRSF tunnel attached");
}

static void detachCrsfTunnel()
{
    if (!s_crsfAttached)
    {
        return;
    }
    crsfRouter.removeConnector(&s_crsfConnector);
    s_crsfConnector.clear();
    s_crsfAttached = false;
    DBGLN("USBConfig: CRSF tunnel detached");
}

static void handlePostCrsf(const HttpRequest &req, HttpResponse &res)
{
    // Deliberately NOT guarded by moduleIsBusy(). These are the same live link parameters the
    // handset LUA already changes in flight, reached through the same endpoint — gating one
    // transport and not the other would make the web UI less capable than the handset for no
    // safety gain. The static-config paths (config/options/reset/reboot) keep their armed guard
    // because they need a reboot to apply.
    if (!configLoaded())
    {
        sendError(res, 404, "no hardware layout");
        return;
    }
    attachCrsfTunnel();

    // The body is one or more whole CRSF frames back to back. Invalid frames are dropped
    // silently, as a CRSF receiver would; the browser retries on its own timeouts.
    size_t offset = 0;
    while (offset + CRSF_MIN_PACKET_LEN <= req.bodyLen)
    {
        const uint8_t *frame = &req.body[offset];
        const uint8_t frameSize = frame[1];
        // frame_size counts everything after itself: type .. crc inclusive.
        const size_t len = (size_t)frameSize + CRSF_FRAME_NOT_COUNTED_BYTES;
        if (frameSize < 2 || len > CRSF_FRAME_SIZE_MAX || offset + len > req.bodyLen)
        {
            break;
        }
        if (crsfRouter.crsf_crc.calc(&frame[2], frameSize - 1, 0) == frame[len - 1])
        {
            crsfRouter.processMessage(&s_crsfConnector, (const crsf_header_t *)frame);
        }
        offset += len;
    }
    res.send(204, "text/plain", nullptr, 0);
}

static void answerCrsfPoll(const HttpDeferred &handle)
{
    uint8_t frames[CRSF_POLL_MAX_BYTES];
    const size_t len = s_crsfConnector.drain(frames, sizeof(frames));
    if (len == 0)
    {
        HttpServer_Respond(handle, 204, "application/octet-stream", nullptr, 0);
    }
    else
    {
        HttpServer_Respond(handle, 200, "application/octet-stream", frames, len);
    }
}

static void handleGetCrsf(const HttpRequest &req, HttpResponse &res)
{
    if (!configLoaded())
    {
        sendError(res, 404, "no hardware layout");
        return;
    }
    attachCrsfTunnel();

    // One poll at a time: a newer one supersedes a waiting one, which is answered empty.
    if (HttpServer_Pending(s_crsfPoll))
    {
        answerCrsfPoll(s_crsfPoll);
    }

    char value[8];
    uint32_t wait = CRSF_POLL_DEFAULT_WAIT_MS;
    if (Http_QueryParam(req.query, "wait", value, sizeof(value)))
    {
        wait = strtoul(value, nullptr, 10);
    }
    if (wait > CRSF_POLL_MAX_WAIT_MS)
    {
        wait = CRSF_POLL_MAX_WAIT_MS;
    }

    s_crsfPoll = res.defer();
    s_crsfPollDeadline = millis() + wait;
    if (s_crsfConnector.hasFrames() || wait == 0)
    {
        answerCrsfPoll(s_crsfPoll);
    }
}

static void serviceCrsfTunnel()
{
    const uint32_t now = millis();
    if (HttpServer_Pending(s_crsfPoll))
    {
        s_crsfLastRequestMs = now; // a waiting poll is a reader
        if (s_crsfConnector.hasFrames() || (int32_t)(now - s_crsfPollDeadline) >= 0)
        {
            answerCrsfPoll(s_crsfPoll);
        }
    }
    else if (s_crsfAttached && now - s_crsfLastRequestMs > CRSF_TUNNEL_IDLE_MS)
    {
        detachCrsfTunnel();
    }
}

// ---------------------------------------------------------------------------------------
// routing
// ---------------------------------------------------------------------------------------
static void handleRequest(const HttpRequest &req, HttpResponse &res)
{
    const bool get = strcmp(req.method, "GET") == 0;
    const bool post = strcmp(req.method, "POST") == 0;
    const char *path = req.path;

    if (strcmp(path, "/hello") == 0 && get)
        handleHello(res);
    else if (strcmp(path, "/config") == 0 && get)
        handleGetConfig(req, res);
    else if ((strcmp(path, "/config") == 0 || strcmp(path, "/import") == 0) && post)
        handleSetConfig(req, res);
    else if (strcmp(path, "/options.json") == 0 && get)
        handleGetOptions(res);
    else if (strcmp(path, "/options.json") == 0 && post)
        handleSetOptions(req, res);
    else if (strcmp(path, "/hardware.json") == 0 && get)
        handleGetHardware(res);
    else if (strcmp(path, "/hardware.json") == 0 && post)
        handleSetHardware(req, res);
    else if (strcmp(path, "/reboot") == 0 && post)
        handleReboot(res);
    else if (strcmp(path, "/reset") == 0 && post)
        handleReset(req, res);
    else if (strcmp(path, "/dfu") == 0 && post)
        handleDfu(res);
    else if (strcmp(path, "/crsf") == 0 && post)
        handlePostCrsf(req, res);
    else if (strcmp(path, "/crsf") == 0 && get)
        handleGetCrsf(req, res);
    else
        sendError(res, 404, "not found");
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
    USBNet_Init();
    HttpServer_Init(handleRequest);
    return DURATION_NEVER; // everything runs from USBConfig_Poll()
}

void USBConfig_Poll()
{
    USBNet_Poll();
    serviceCrsfTunnel();
}

device_t USBConfig_device = {
    .initialize = initialize,
    .start = start,
    .event = nullptr,
    .timeout = nullptr,
    .subscribe = EVENT_NONE
};

#endif /* PLATFORM_STM32 */
