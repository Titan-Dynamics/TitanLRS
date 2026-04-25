#include "ST67WifiOtaMode.h"

#include <cstring>
#include <cstdlib>
#include <stm32h7xx_hal.h>
#include <ArduinoJson.h>

#include "WebPage.h"
#include "ConfigJSON.h"
#include "options.h"
#include "common.h"

#if defined(TARGET_RX)
#include "config.h"
#elif defined(TARGET_TX)
#include "config.h"
#endif

// ============================================================================
// Static helpers — path/param parsing (not class members)
// ============================================================================

// Check if the HTTP request URL path exactly matches `path`.
// Matches /path followed by ' ', '?', '\r', '\n', or end-of-string.
static bool pathMatch(const char* request, const char* path)
{
    const char* p = strchr(request, '/');
    if (!p) return false;
    size_t len = strlen(path);
    if (strncmp(p, path, len) != 0) return false;
    char next = p[len];
    return next == ' ' || next == '?' || next == '\r' || next == '\n' || next == '\0';
}

// Find the URL query string (the part between '?' and ' HTTP/').
static void extractQueryString(const char* request, const char** qs, int* qsLen)
{
    *qs = nullptr;
    *qsLen = 0;
    const char* httpVer = strstr(request, " HTTP/");
    if (!httpVer) return;
    for (const char* p = request; p < httpVer; p++) {
        if (*p == '?') {
            *qs = p + 1;
            *qsLen = static_cast<int>(httpVer - (p + 1));
            return;
        }
    }
}

// Check if `key` is present as a standalone param key in a query/form string.
static bool paramPresent(const char* str, int len, const char* key)
{
    if (!str || len <= 0) return false;
    int keyLen = static_cast<int>(strlen(key));
    int i = 0;
    while (i < len) {
        if (strncmp(str + i, key, keyLen) == 0) {
            int j = i + keyLen;
            if (j >= len) return true;
            char next = str[j];
            if (next == '=' || next == '&' || next == '\r' || next == '\n' || next == ' ')
                return true;
        }
        // Advance to next param (skip to '&')
        while (i < len && str[i] != '&') i++;
        i++; // skip '&'
    }
    return false;
}

// Extract the URL-decoded value of `key` from a query/form string.
// Returns true and fills valueBuf if found (including flag params with empty value).
static bool paramExtract(const char* str, int len, const char* key,
                         char* valueBuf, int valueBufLen)
{
    if (!str || len <= 0) return false;
    int keyLen = static_cast<int>(strlen(key));
    int i = 0;
    while (i < len) {
        if (strncmp(str + i, key, keyLen) == 0) {
            int j = i + keyLen;
            if (j < len && str[j] == '=') {
                j++;
                int vStart = j;
                while (j < len && str[j] != '&' && str[j] != '\r' && str[j] != '\n' && str[j] != ' ')
                    j++;
                if (valueBuf && valueBufLen > 0) {
                    int out = 0;
                    for (int k = vStart; k < j && out < valueBufLen - 1; k++) {
                        char c = str[k];
                        if (c == '%' && k + 2 < j) {
                            char hex[3] = {str[k+1], str[k+2], 0};
                            valueBuf[out++] = static_cast<char>(strtol(hex, nullptr, 16));
                            k += 2;
                        } else if (c == '+') {
                            valueBuf[out++] = ' ';
                        } else {
                            valueBuf[out++] = c;
                        }
                    }
                    valueBuf[out] = '\0';
                }
                return true;
            }
            // Flag param (no '=' value)
            if (j >= len || str[j] == '&' || str[j] == '\r' || str[j] == '\n' || str[j] == ' ') {
                if (valueBuf && valueBufLen > 0) valueBuf[0] = '\0';
                return true;
            }
        }
        while (i < len && str[i] != '&') i++;
        i++;
    }
    return false;
}

// ============================================================================
// ST67WifiOtaMode::start / service
// ============================================================================

bool ST67WifiOtaMode::start()
{
    if (_active) {
        return true;
    }

    Serial.println(F("\n========================================"));
    Serial.println(F("  Entering ST67 WiFi OTA mode"));
    Serial.println(F("========================================"));
    Serial.flush();
    delay(50);

    if (!_wifi.begin()) {
        Serial.println(F("FATAL: ST67 module did not respond."));
        return false;
    }

    if (!_wifi.startAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, AP_ECN)) {
        Serial.println(F("FATAL: Could not start Soft-AP"));
        return false;
    }

    if (!_wifi.startDHCP()) {
        Serial.println(F("FATAL: Could not enable DHCP"));
        return false;
    }

    char ipBuf[32] = {0};
    if (_wifi.getAPIP(ipBuf, sizeof(ipBuf))) {
        Serial.printf("AP IP: %s\n", ipBuf);
        Serial.printf("Connect to Wi-Fi \"%s\"  password \"%s\"\n", AP_SSID, AP_PASSWORD);
        Serial.printf("Then open http://%s in a browser.\n", ipBuf);
    } else {
        Serial.println(F("WARNING: Could not read AP IP address"));
    }

    if (!_wifi.startTCPServer(80)) {
        Serial.println(F("FATAL: Could not start TCP server"));
        return false;
    }

    _wifi.at.sendCommand("AT+CIPSTO=120");
    Serial.println(F("WiFi OTA mode is active"));

    _active = true;
    return true;
}

void ST67WifiOtaMode::service()
{
    if (!_active) {
        return;
    }

    if (_pendingReboot && millis() >= _rebootAt) {
        Serial.println(F("[OTA] Rebooting now..."));
        delay(100);
        NVIC_SystemReset();
    }

    if (_otaUpdater.isStarted() && _updateState == UpdateState::IDLE &&
        _eraseDeadlineMs > 0 && millis() >= _eraseDeadlineMs) {
        Serial.println(F("[OTA] Erase watchdog: no upload received, aborting"));
        _otaUpdater.abort();
        _eraseDeadlineMs = 0;
    }

    int linkId = -1;
    int n = _wifi.checkForClient(_clientBuf, sizeof(_clientBuf), &linkId);

    if (n > 0 && linkId >= 0) {
        if (_updateState == UpdateState::RECEIVING && linkId == _updateLinkId) {
            if (looksLikeHttpRequest(_clientBuf, n)) {
                Serial.printf("[OTA] Upload stream replaced by new request on link %d, aborting upload\n", linkId);
                _otaUpdater.abort();
                _updateState = UpdateState::IDLE;
                _updateLinkId = -1;
                _eraseDeadlineMs = 0;
                _uploadDiscPending = false;
                handleClientRequest(linkId, _clientBuf, n);
                return;
            }

            _updateLastDataMs = millis();
            if (_uploadDiscPending) {
                Serial.println(F("[OTA] Upload resumed"));
                _uploadDiscPending = false;
            }
            handleUploadData(linkId, _clientBuf, n);
        } else if (_postLinkId >= 0 && linkId == _postLinkId) {
            // Continuation frame for a POST whose body hadn't fully arrived yet.
            accumulatePost(linkId, _clientBuf, n);
        } else {
            handleClientRequest(linkId, _clientBuf, n);
        }
    } else if (n == 0 && _updateState == UpdateState::RECEIVING) {
        const char* cip = strstr(_clientBuf, "+CIP:");
        int evtLink = -1;
        if (cip) {
            cip += 5;
            int v = 0;
            bool hasDigit = false;
            while (*cip >= '0' && *cip <= '9') {
                hasDigit = true;
                v = v * 10 + (*cip - '0');
                cip++;
            }
            if (hasDigit) {
                evtLink = v;
            }
        }

        const bool isDisc = (strstr(_clientBuf, "DISCONNECTED") != nullptr);
        const bool isConn = (strstr(_clientBuf, "CONNECTED") != nullptr) && !isDisc;

        if (evtLink == _updateLinkId && isDisc && (millis() - _updateStartMs) > 2000) {
            if (!_uploadDiscPending) {
                _uploadDiscPending = true;
                Serial.printf("[OTA] Upload link %d disconnect seen\n", evtLink);
            }
        } else if (evtLink == _updateLinkId && isConn) {
            if (_uploadDiscPending) {
                _uploadDiscPending = false;
                Serial.printf("[OTA] Upload link %d reconnected\n", evtLink);
            }
        }
    }

    if (_updateState == UpdateState::RECEIVING &&
        (millis() - _updateLastDataMs) > UPLOAD_STALL_TIMEOUT_MS) {
        Serial.println(F("[OTA] Upload stalled, aborting"));
        _otaUpdater.abort();
        _updateState = UpdateState::IDLE;
        _updateLinkId = -1;
        _eraseDeadlineMs = 0;
        _uploadDiscPending = false;
    }

    if (_updateState == UpdateState::IDLE) {
        delay(5);
    }
}

// ============================================================================
// Response helpers
// ============================================================================

void ST67WifiOtaMode::sendHttpResponse(int linkId, const char* status,
                                       const char* contentType, const char* body,
                                       bool closeConn)
{
    char hdr[256];
    uint16_t bodyLen = body ? static_cast<uint16_t>(strlen(body)) : 0;
    int hdrLen = snprintf(hdr, sizeof(hdr),
                          "HTTP/1.1 %s\r\n"
                          "Content-Type: %s\r\n"
                          "Content-Length: %u\r\n"
                          "Connection: %s\r\n"
                          "\r\n",
                          status, contentType, bodyLen, closeConn ? "close" : "keep-alive");

    _wifi.sendResponse(linkId, reinterpret_cast<const uint8_t*>(hdr), static_cast<uint16_t>(hdrLen));
    if (bodyLen > 0) {
        _wifi.sendResponse(linkId, reinterpret_cast<const uint8_t*>(body), bodyLen);
    }

    if (closeConn) {
        delay(50);
        _wifi.closeConnection(linkId);
    }
}

// ============================================================================
// Request parsing helpers
// ============================================================================

const char* ST67WifiOtaMode::extractBody(const char* request, int reqLen, int* bodyLen) const
{
    const char* bodyStart = strstr(request, "\r\n\r\n");
    if (!bodyStart) { *bodyLen = 0; return nullptr; }
    bodyStart += 4;
    int32_t cl = parseContentLength(request);
    if (cl > 0) {
        *bodyLen = static_cast<int>(cl);
    } else {
        *bodyLen = reqLen - static_cast<int>(bodyStart - request);
        if (*bodyLen < 0) *bodyLen = 0;
    }
    return bodyStart;
}

bool ST67WifiOtaMode::hasParam(const char* request, int reqLen, const char* name) const
{
    const char* qs; int qsLen;
    extractQueryString(request, &qs, &qsLen);
    if (paramPresent(qs, qsLen, name)) return true;

    int bodyLen = 0;
    const char* body = extractBody(request, reqLen, &bodyLen);
    return paramPresent(body, bodyLen, name);
}

bool ST67WifiOtaMode::getParam(const char* request, int reqLen, const char* name,
                                char* valueBuf, int valueBufLen) const
{
    const char* qs; int qsLen;
    extractQueryString(request, &qs, &qsLen);
    if (paramExtract(qs, qsLen, name, valueBuf, valueBufLen)) return true;

    int bodyLen = 0;
    const char* body = extractBody(request, reqLen, &bodyLen);
    return paramExtract(body, bodyLen, name, valueBuf, valueBufLen);
}

// ============================================================================
// POST body accumulation
// ============================================================================

void ST67WifiOtaMode::accumulatePost(int linkId, const char* data, int len)
{
    int space = POST_BUF_SIZE - _postBufUsed - 1;
    int copy  = (len < space) ? len : space;
    if (copy > 0) {
        memcpy(_postBuf + _postBufUsed, data, copy);
        _postBufUsed += copy;
        _postBuf[_postBufUsed] = '\0';
    }

    if (_postBufUsed >= _postExpected) {
        // Full request assembled — dispatch.
        _postLinkId = -1;
        int used = _postBufUsed;
        _postBufUsed  = 0;
        _postExpected = 0;
        handleClientRequest(linkId, _postBuf, used);
    }
}

// ============================================================================
// Request dispatch
// ============================================================================

void ST67WifiOtaMode::handleClientRequest(int linkId, const char* request, int reqLen)
{
    const char* eol = strchr(request, '\r');
    if (eol) {
        char line[128];
        int lineLen = static_cast<int>(eol - request);
        if (lineLen > static_cast<int>(sizeof(line) - 1)) lineLen = sizeof(line) - 1;
        memcpy(line, request, lineLen);
        line[lineLen] = '\0';
        Serial.printf("[HTTP] %s\n", line);
    }

    const bool isGet  = (strncmp(request, "GET ",  4) == 0);
    const bool isPost = (strncmp(request, "POST ", 5) == 0);

    // POST body buffering: the ST67 sometimes delivers headers and body in
    // separate SPI frames. Detect this and accumulate before dispatching.
    // Skip buffering for /upload — it uses the streaming state machine and
    // handles partial body data itself; buffering a multi-hundred-KB binary
    // into a 2 KB buffer would loop forever.
    if (isPost && !pathMatch(request, "/upload")) {
        const char* headersEnd = strstr(request, "\r\n\r\n");
        if (headersEnd) {
            int32_t cl = parseContentLength(request);
            if (cl > 0) {
                int headersLen = static_cast<int>(headersEnd + 4 - request);
                int bodyAvail  = reqLen - headersLen;
                if (bodyAvail < static_cast<int>(cl)) {
                    // Body not fully arrived — start accumulation.
                    int expected = headersLen + static_cast<int>(cl);
                    int copy = (reqLen < POST_BUF_SIZE - 1) ? reqLen : POST_BUF_SIZE - 1;
                    memcpy(_postBuf, request, copy);
                    _postBuf[copy] = '\0';
                    _postBufUsed  = copy;
                    _postExpected = (expected < POST_BUF_SIZE - 1) ? expected : POST_BUF_SIZE - 1;
                    _postLinkId   = linkId;
                    Serial.printf("[HTTP] POST body split: %d/%ld bytes, buffering\n",
                                  bodyAvail, (long)cl);
                    return;
                }
            }
        }
    }

    // Config endpoints (POST before GET to disambiguate)
    if      (isPost && pathMatch(request, "/config"))       { handlePostConfig(linkId, request, reqLen); }
    else if (isGet  && pathMatch(request, "/config"))       { handleGetConfig(linkId, request); }
    else if (isGet  && pathMatch(request, "/heartbeat"))    { handleGetHeartbeat(linkId); }
    // Options endpoints
    else if (isPost && pathMatch(request, "/options.json")) { handlePostOptions(linkId, request, reqLen); }
    else if (isGet  && pathMatch(request, "/options.json")) { handleGetOptions(linkId); }
    else if (isGet  && pathMatch(request, "/options"))      { serveAsset(linkId, "/options.html"); }
    // Reboot / reset
    else if (isGet  && pathMatch(request, "/reboot"))       { handleGetReboot(linkId); }
    else if (isGet  && pathMatch(request, "/reset"))        { handleGetReset(linkId, request, reqLen); }
    // WiFi management
    else if (isGet  && pathMatch(request, "/networks.json")){ handleGetNetworks(linkId); }
    else if (isPost && pathMatch(request, "/sethome"))      { handlePostSethome(linkId, request, reqLen); }
    else if (isPost && pathMatch(request, "/forget"))       { handlePostForget(linkId, request, reqLen); }
    else if (isGet  && pathMatch(request, "/wifi"))         { serveAsset(linkId, "/wifi.html"); }
#if defined(TARGET_TX)
    // TX-only endpoints
    else if (isPost && pathMatch(request, "/import"))       { handlePostImport(linkId, request, reqLen); }
    else if (isGet  && pathMatch(request, "/import"))       { serveAsset(linkId, "/import.html"); }
    else if (isPost && pathMatch(request, "/buttons"))      { handlePostButtons(linkId, request, reqLen); }
    else if (isGet  && pathMatch(request, "/buttons"))      { serveAsset(linkId, "/buttons.html"); }
#endif
    // OTA endpoints
    else if (isPost && pathMatch(request, "/erase"))        { handlePostErase(linkId, request, reqLen); }
    else if (isPost && pathMatch(request, "/upload"))       { handlePostUpload(linkId, request, reqLen); }
    // Static pages
    else if (isGet  && pathMatch(request, "/binding"))      { serveAsset(linkId, "/binding.html"); }
    else if (isGet  && pathMatch(request, "/update"))       { serveAsset(linkId, "/update.html"); }
    else if (isGet  && (pathMatch(request, "/") || strstr(request, "GET /index"))) {
        serveAsset(linkId, "/index.html");
    }
    else { sendHttpResponse(linkId, "404 Not Found", "text/plain", "404 - Not Found"); }
}

// ============================================================================
// GET /config  (and GET /config?export)
// ============================================================================

void ST67WifiOtaMode::handleGetConfig(int linkId, const char* request)
{
    bool exportMode = hasParam(request, 0, "export");

    // TX export with 64 model configs needs a large buffer; RX/non-export fits in 2KB.
    // Allocate from heap to avoid stack overflow in the export case.
    const size_t bufSize = exportMode ? 16384 : 2048;
    char* body = new char[bufSize];
    if (!body) {
        sendHttpResponse(linkId, "500 Internal Server Error", "text/plain", "Out of memory");
        return;
    }

    int bodyLen = buildConfigJSON(body, bufSize, exportMode);

    char hdr[256];
    int hdrLen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n", bodyLen);

    _wifi.sendResponse(linkId, reinterpret_cast<const uint8_t*>(hdr), static_cast<uint16_t>(hdrLen));
    _wifi.sendResponse(linkId, reinterpret_cast<const uint8_t*>(body), static_cast<uint16_t>(bodyLen));
    delay(50);
    _wifi.closeConnection(linkId);

    delete[] body;
}

// ============================================================================
// GET /heartbeat
// ============================================================================

void ST67WifiOtaMode::handleGetHeartbeat(int linkId)
{
    sendHttpResponse(linkId, "200 OK", "application/json", "{\"ok\":true}");
}

// ============================================================================
// GET /reboot
// ============================================================================

void ST67WifiOtaMode::handleGetReboot(int linkId)
{
    // Match ESP response: "Kill -9, no more CPU time!" with Connection: close
    sendHttpResponse(linkId, "200 OK", "application/json", "Kill -9, no more CPU time!");
    _pendingReboot = true;
    _rebootAt = millis() + 100;
}

// ============================================================================
// GET /reset?options&model
// ============================================================================

void ST67WifiOtaMode::handleGetReset(int linkId, const char* request, int reqLen)
{
    if (hasParam(request, reqLen, "options")) {
        resetOptionsToDefaults();
#if defined(TARGET_RX)
        // Match ESP: also reset model ID and force-tlm when resetting options
        config.SetModelId(255);
        config.SetForceTlmOff(false);
        config.Commit();
#endif
    }
    if (hasParam(request, reqLen, "model") || hasParam(request, reqLen, "config")) {
        config.SetDefaults(true);
    }
    sendHttpResponse(linkId, "200 OK", "application/json", "Reset complete, rebooting...");
    _pendingReboot = true;
    _rebootAt = millis() + 100;
}

// ============================================================================
// GET /options.json
// ============================================================================

void ST67WifiOtaMode::handleGetOptions(int linkId)
{
    char body[512];
    int bodyLen = buildOptionsJSON(body, sizeof(body));

    char hdr[256];
    int hdrLen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n", bodyLen);

    _wifi.sendResponse(linkId, reinterpret_cast<const uint8_t*>(hdr), static_cast<uint16_t>(hdrLen));
    _wifi.sendResponse(linkId, reinterpret_cast<const uint8_t*>(body), static_cast<uint16_t>(bodyLen));
    delay(50);
    _wifi.closeConnection(linkId);
}

// ============================================================================
// POST /config
// ============================================================================

void ST67WifiOtaMode::handlePostConfig(int linkId, const char* request, int reqLen)
{
    int bodyLen = 0;
    const char* body = extractBody(request, reqLen, &bodyLen);
    if (!body || bodyLen <= 0) {
        sendHttpResponse(linkId, "400 Bad Request", "text/plain", "Missing body");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, static_cast<size_t>(bodyLen));
    if (err) {
        sendHttpResponse(linkId, "400 Bad Request", "text/plain", "Invalid JSON");
        return;
    }

#if defined(TARGET_RX)
    if (!doc["serial-protocol"].isNull())
        config.SetSerialProtocol((eSerialProtocol)doc["serial-protocol"].as<uint8_t>());
    if (!doc["sbus-failsafe"].isNull())
        config.SetFailsafeMode((eFailsafeMode)doc["sbus-failsafe"].as<uint8_t>());
    if (!doc["modelid"].isNull()) {
        long modelid = doc["modelid"].as<long>();
        if (modelid < 0 || modelid > 63) modelid = 255;
        config.SetModelId(static_cast<uint8_t>(modelid));
    }
    if (!doc["force-tlm"].isNull())
        config.SetForceTlmOff(doc["force-tlm"].as<bool>());
    if (!doc["vbind"].isNull())
        config.SetBindStorage((rx_config_bindstorage_t)doc["vbind"].as<uint8_t>());
    if (doc["uid"].is<JsonArray>()) {
        JsonArray juid = doc["uid"].as<JsonArray>();
        uint8_t newUid[UID_LEN] = {0};
        size_t juidLen = juid.size() < UID_LEN ? juid.size() : UID_LEN;
        // Right-justify into newUid (supports 4-byte OTA-bound UID)
        for (size_t i = 0; i < juidLen; i++)
            newUid[UID_LEN - juidLen + i] = juid[i].as<uint8_t>();
        if (memcmp(newUid, config.GetUID(), UID_LEN) != 0) {
            config.SetUID(newUid);
            memcpy(UID, newUid, UID_LEN);
        }
    }
    if (doc["pwm"].is<JsonArray>()) {
        JsonArray pwm = doc["pwm"].as<JsonArray>();
        for (uint32_t ch = 0; ch < pwm.size(); ch++) {
            config.SetPwmChannelRaw(static_cast<uint8_t>(ch), pwm[ch].as<uint32_t>());
        }
    }
    config.Commit();

#elif defined(TARGET_TX)
    if (doc["button-actions"].is<JsonArray>()) {
        JsonArray array = doc["button-actions"].as<JsonArray>();
        for (size_t button = 0; button < array.size() && button < 2; button++) {
            tx_button_color_t action = {};
            action.val.color = array[button]["color"].as<uint8_t>();
            for (int act = 0; act < CONFIG_TX_BUTTON_ACTION_CNT; act++) {
                action.val.actions[act].pressType = array[button]["action"][act]["is-long-press"].as<bool>();
                action.val.actions[act].count     = array[button]["action"][act]["count"].as<uint8_t>();
                action.val.actions[act].action    = array[button]["action"][act]["action"].as<uint8_t>();
            }
            config.SetButtonActions(static_cast<uint8_t>(button), &action);
        }
    }
    config.Commit();
#endif

    sendHttpResponse(linkId, "200 OK", "text/plain", "Configuration updated");
}

// ============================================================================
// POST /options.json
// ============================================================================

void ST67WifiOtaMode::handlePostOptions(int linkId, const char* request, int reqLen)
{
    int bodyLen = 0;
    const char* body = extractBody(request, reqLen, &bodyLen);
    if (!body || bodyLen <= 0) {
        sendHttpResponse(linkId, "400 Bad Request", "text/plain", "Missing body");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, static_cast<size_t>(bodyLen));
    if (err) {
        sendHttpResponse(linkId, "400 Bad Request", "text/plain", "Invalid JSON");
        return;
    }

    // Validate flash-discriminator to prevent stale writes (matches ESP behaviour)
    if (firmwareOptions.flash_discriminator != doc["flash-discriminator"].as<uint32_t>()) {
        sendHttpResponse(linkId, "409 Conflict", "text/plain",
                         "Mismatched device identifier, refresh the page and try again.");
        return;
    }

    // Apply fields from JSON
    if (doc["uid"].is<JsonArray>()) {
        JsonArray uid = doc["uid"].as<JsonArray>();
        size_t len = uid.size() < 6 ? uid.size() : 6;
        for (size_t i = 0; i < len; i++)
            firmwareOptions.uid[i] = uid[i].as<uint8_t>();
        firmwareOptions.hasUID = (len > 0);
    }
    if (!doc["wifi-on-interval"].isNull()) {
        int32_t interval = doc["wifi-on-interval"].as<int32_t>();
        firmwareOptions.wifi_auto_on_interval = (interval < 0) ? -1 : interval * 1000;
    }
    if (!doc["wifi-ssid"].isNull())
        strlcpy(firmwareOptions.home_wifi_ssid, doc["wifi-ssid"] | "", sizeof(firmwareOptions.home_wifi_ssid));
    if (!doc["wifi-password"].isNull())
        strlcpy(firmwareOptions.home_wifi_password, doc["wifi-password"] | "", sizeof(firmwareOptions.home_wifi_password));
    if (!doc["domain"].isNull())
        firmwareOptions.domain = doc["domain"].as<uint8_t>();
    if (!doc["is-airport"].isNull())
        firmwareOptions.is_airport = doc["is-airport"].as<bool>();
#if defined(TARGET_RX)
    if (!doc["rcvr-uart-baud"].isNull())
        firmwareOptions.uart_baud = doc["rcvr-uart-baud"].as<uint32_t>();
    if (!doc["lock-on-first-connection"].isNull())
        firmwareOptions.lock_on_first_connection = doc["lock-on-first-connection"].as<bool>();
#elif defined(TARGET_TX)
    if (!doc["tlm-interval"].isNull())
        firmwareOptions.tlm_report_interval = doc["tlm-interval"].as<uint32_t>();
    if (!doc["fan-runtime"].isNull())
        firmwareOptions.fan_min_runtime = doc["fan-runtime"].as<uint32_t>();
    if (!doc["unlock-higher-power"].isNull())
        firmwareOptions.unlock_higher_power = doc["unlock-higher-power"].as<bool>();
    if (!doc["airport-uart-baud"].isNull())
        firmwareOptions.uart_baud = doc["airport-uart-baud"].as<uint32_t>();
#endif

    saveOptions();
    sendHttpResponse(linkId, "200 OK", "text/plain", "Options updated");
}

// ============================================================================
// GET /networks.json
// ============================================================================

void ST67WifiOtaMode::handleGetNetworks(int linkId)
{
    // TODO: Use AT+CWLAP to scan for networks via ST67 AT command interface.
    // AT+CWLAP is blocking and can take several seconds, which would stall the
    // HTTP server. Implement asynchronous scanning before enabling this.
    // For now, return an empty array so the WiFi page loads without errors.
    sendHttpResponse(linkId, "200 OK", "application/json", "[]");
}

// ============================================================================
// POST /sethome
// ============================================================================

void ST67WifiOtaMode::handlePostSethome(int linkId, const char* request, int reqLen)
{
    char network[33]  = {0};
    char password[65] = {0};
    char onIntervalBuf[16] = {0};

    getParam(request, reqLen, "network",          network,       sizeof(network));
    getParam(request, reqLen, "password",          password,      sizeof(password));
    getParam(request, reqLen, "wifi-on-interval",  onIntervalBuf, sizeof(onIntervalBuf));

    if (hasParam(request, reqLen, "save")) {
        strlcpy(firmwareOptions.home_wifi_ssid,     network,  sizeof(firmwareOptions.home_wifi_ssid));
        strlcpy(firmwareOptions.home_wifi_password, password, sizeof(firmwareOptions.home_wifi_password));
        int32_t interval = onIntervalBuf[0] ? static_cast<int32_t>(atoi(onIntervalBuf)) : -1;
        firmwareOptions.wifi_auto_on_interval = (interval < 0) ? -1 : interval * 1000;
        saveOptions();
    }

    // Note: STM32/ST67 is AP-only and cannot switch to STA mode.
    // Credentials are saved for future use but no connection is attempted.
    sendHttpResponse(linkId, "200 OK", "text/plain", "WiFi credentials saved.");
}

// ============================================================================
// POST /forget
// ============================================================================

void ST67WifiOtaMode::handlePostForget(int linkId, const char* request, int reqLen)
{
    char onIntervalBuf[16] = {0};
    getParam(request, reqLen, "wifi-on-interval", onIntervalBuf, sizeof(onIntervalBuf));

    firmwareOptions.home_wifi_ssid[0]     = 0;
    firmwareOptions.home_wifi_password[0] = 0;
    int32_t interval = onIntervalBuf[0] ? static_cast<int32_t>(atoi(onIntervalBuf)) : -1;
    firmwareOptions.wifi_auto_on_interval = (interval < 0) ? -1 : interval * 1000;
    saveOptions();

    sendHttpResponse(linkId, "200 OK", "text/plain", "Home network forgotten.");
}

// ============================================================================
// TX-only: POST /import  and  POST /buttons
// ============================================================================

#if defined(TARGET_TX)

void ST67WifiOtaMode::handlePostImport(int linkId, const char* request, int reqLen)
{
    int bodyLen = 0;
    const char* body = extractBody(request, reqLen, &bodyLen);
    if (!body || bodyLen <= 0) {
        sendHttpResponse(linkId, "400 Bad Request", "text/plain", "Missing body");
        return;
    }

    // Import JSON can be large (64 model configs). Use heap allocation.
    JsonDocument* docPtr = new JsonDocument();
    if (!docPtr) {
        sendHttpResponse(linkId, "500 Internal Server Error", "text/plain", "Out of memory");
        return;
    }
    JsonDocument& doc = *docPtr;

    DeserializationError err = deserializeJson(doc, body, static_cast<size_t>(bodyLen));
    if (err) {
        delete docPtr;
        sendHttpResponse(linkId, "400 Bad Request", "text/plain", "Invalid JSON");
        return;
    }

    // Unwrap outer "config" key if present (matches ESP ImportConfiguration)
    JsonVariant cfg = doc["config"].is<JsonObject>() ? doc["config"] : doc.as<JsonVariant>();

    if (!cfg["fan-mode"].isNull())           config.SetFanMode(cfg["fan-mode"].as<uint8_t>());
    if (!cfg["power-fan-threshold"].isNull()) config.SetPowerFanThreshold(cfg["power-fan-threshold"].as<uint8_t>());
    if (!cfg["motion-mode"].isNull())         config.SetMotionMode(cfg["motion-mode"].as<uint8_t>());

    if (cfg["vtx-admin"].is<JsonObject>()) {
        JsonObject vtx = cfg["vtx-admin"].as<JsonObject>();
        if (!vtx["band"].isNull())    config.SetVtxBand(vtx["band"].as<uint8_t>());
        if (!vtx["channel"].isNull()) config.SetVtxChannel(vtx["channel"].as<uint8_t>());
        if (!vtx["pitmode"].isNull()) config.SetVtxPitmode(vtx["pitmode"].as<uint8_t>());
        if (!vtx["power"].isNull())   config.SetVtxPower(vtx["power"].as<uint8_t>());
    }

    if (cfg["backpack"].is<JsonObject>()) {
        JsonObject bp = cfg["backpack"].as<JsonObject>();
        if (!bp["disabled"].isNull())         config.SetBackpackDisable(bp["disabled"].as<bool>());
        if (!bp["dvr-start-delay"].isNull())  config.SetDvrStartDelay(bp["dvr-start-delay"].as<uint8_t>());
        if (!bp["dvr-stop-delay"].isNull())   config.SetDvrStopDelay(bp["dvr-stop-delay"].as<uint8_t>());
        if (!bp["dvr-aux-channel"].isNull())  config.SetDvrAux(bp["dvr-aux-channel"].as<uint8_t>());
        if (!bp["telemetry-mode"].isNull())   config.SetBackpackTlmMode(bp["telemetry-mode"].as<uint8_t>());
    }

    if (cfg["model"].is<JsonObject>()) {
        for (JsonPair kv : cfg["model"].as<JsonObject>()) {
            uint8_t model = static_cast<uint8_t>(atoi(kv.key().c_str()));
            JsonObject mc = kv.value().as<JsonObject>();
            config.SetModelId(model);
            if (!mc["packet-rate"].isNull())     config.SetRate(mc["packet-rate"].as<uint8_t>());
            if (!mc["telemetry-ratio"].isNull())  config.SetTlm(mc["telemetry-ratio"].as<uint8_t>());
            if (!mc["switch-mode"].isNull())      config.SetSwitchMode(mc["switch-mode"].as<uint8_t>());
            if (!mc["link-mode"].isNull())        config.SetLinkMode(mc["link-mode"].as<uint8_t>());
            if (!mc["model-match"].isNull())      config.SetModelMatch(mc["model-match"].as<bool>());
            if (!mc["tx-antenna"].isNull())       config.SetAntennaMode(mc["tx-antenna"].as<uint8_t>());
            if (!mc["ptr-start-chan"].isNull())   config.SetPTRStartChannel(mc["ptr-start-chan"].as<uint8_t>());
            if (!mc["ptr-enable-chan"].isNull())  config.SetPTREnableChannel(mc["ptr-enable-chan"].as<uint8_t>());
            if (mc["power"].is<JsonObject>()) {
                JsonObject pwr = mc["power"].as<JsonObject>();
                if (!pwr["max-power"].isNull())      config.SetPower(pwr["max-power"].as<uint8_t>());
                if (!pwr["dynamic-power"].isNull())  config.SetDynamicPower(pwr["dynamic-power"].as<bool>());
                if (!pwr["boost-channel"].isNull())  config.SetBoostChannel(pwr["boost-channel"].as<uint8_t>());
            }
            config.Commit(); // commit after each model update
        }
    }

    // Also apply button-actions from the imported config
    if (cfg["button-actions"].is<JsonArray>()) {
        JsonArray array = cfg["button-actions"].as<JsonArray>();
        for (size_t button = 0; button < array.size() && button < 2; button++) {
            tx_button_color_t action = {};
            action.val.color = array[button]["color"].as<uint8_t>();
            for (int act = 0; act < CONFIG_TX_BUTTON_ACTION_CNT; act++) {
                action.val.actions[act].pressType = array[button]["action"][act]["is-long-press"].as<bool>();
                action.val.actions[act].count     = array[button]["action"][act]["count"].as<uint8_t>();
                action.val.actions[act].action    = array[button]["action"][act]["action"].as<uint8_t>();
            }
            config.SetButtonActions(static_cast<uint8_t>(button), &action);
        }
        config.Commit();
    }

    delete docPtr;
    sendHttpResponse(linkId, "200 OK", "text/plain", "Import/update complete");
}

void ST67WifiOtaMode::handlePostButtons(int linkId, const char* request, int reqLen)
{
    int bodyLen = 0;
    const char* body = extractBody(request, reqLen, &bodyLen);
    if (!body || bodyLen <= 0) {
        sendHttpResponse(linkId, "200 OK", "text/plain", "OK");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, body, static_cast<size_t>(bodyLen)) == DeserializationError::Ok
        && doc.is<JsonArray>()) {
        (void)doc[0].as<int>(); // color1 — placeholder until STM32 TX button LEDs confirmed
        (void)doc[1].as<int>(); // color2
        // TODO: Call setButtonColors(color1, color2) once STM32 TX button LED
        // support is confirmed (requires checking GPIO_PIN_BUTTON / GPIO_PIN_BUTTON2).
    }
    sendHttpResponse(linkId, "200 OK", "text/plain", "OK");
}

#endif // TARGET_TX

// ============================================================================
// Asset serving
// ============================================================================

void ST67WifiOtaMode::serveAsset(int linkId, const char* path)
{
    for (size_t i = 0; i < WEB_ASSETS_COUNT; i++) {
        if (strcmp(path, WEB_ASSETS[i].path) == 0) {
            char hdr[256];
            int hdrLen = snprintf(hdr, sizeof(hdr),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: %s\r\n"
                "Content-Encoding: gzip\r\n"
                "Content-Length: %u\r\n"
                "Cache-Control: no-store\r\n"
                "Connection: close\r\n"
                "\r\n",
                WEB_ASSETS[i].content_type,
                (unsigned)WEB_ASSETS[i].size);

            bool ok = _wifi.sendResponse(linkId, reinterpret_cast<const uint8_t*>(hdr), static_cast<uint16_t>(hdrLen));
            if (ok) {
                ok = _wifi.sendResponse(linkId, WEB_ASSETS[i].data, static_cast<uint16_t>(WEB_ASSETS[i].size));
            }
            // Wait for the ST67 to deliver all TCP data before closing.
            delay(ok ? 300 : 50);
            _wifi.closeConnection(linkId);
            return;
        }
    }
    sendHttpResponse(linkId, "404 Not Found", "text/plain", "Not found");
}

// ============================================================================
// OTA upload handlers (unchanged)
// ============================================================================

void ST67WifiOtaMode::handlePostErase(int linkId, const char* request, int reqLen)
{
    (void)reqLen;

    if (_pendingReboot) {
        sendHttpResponse(linkId, "503 Service Unavailable", "text/plain", "Rebooting after previous update");
        return;
    }

    int32_t imageSize = parseImageSize(request);
    if (imageSize <= 0) {
        imageSize = parseContentLength(request);
    }
    if (imageSize <= 0) {
        sendHttpResponse(linkId, "400 Bad Request", "text/plain", "Missing X-Image-Size header");
        return;
    }

    Serial.printf("[OTA] POST /erase image size = %ld bytes\n", static_cast<long>(imageSize));
    Serial.println(F("[OTA] Erasing inactive bank..."));

    if (!_otaUpdater.begin(static_cast<uint32_t>(imageSize))) {
        char errBuf[128];
        snprintf(errBuf, sizeof(errBuf), "Erase failed: %s", _otaUpdater.lastError());
        Serial.printf("[OTA] %s\n", errBuf);
        sendHttpResponse(linkId, "500 Internal Server Error", "text/plain", errBuf);
        return;
    }

    drainSpiFrames();

    _eraseDeadlineMs = millis() + ERASE_TIMEOUT_MS;
    sendHttpResponse(linkId, "200 OK", "text/plain", "ready", false);
}

void ST67WifiOtaMode::handlePostUpload(int linkId, const char* request, int reqLen)
{
    if (_pendingReboot) {
        sendHttpResponse(linkId, "503 Service Unavailable", "text/plain", "Rebooting after previous update");
        return;
    }

    if (!_otaUpdater.isStarted()) {
        sendHttpResponse(linkId, "400 Bad Request", "text/plain", "Flash not erased - call POST /erase first");
        return;
    }

    if (_updateState != UpdateState::IDLE) {
        sendHttpResponse(linkId, "409 Conflict", "text/plain", "Upload already in progress");
        return;
    }

    int32_t contentLength = parseContentLength(request);
    if (contentLength <= 0) {
        sendHttpResponse(linkId, "400 Bad Request", "text/plain", "Missing Content-Length");
        return;
    }

    if (static_cast<uint32_t>(contentLength) != _otaUpdater.imageSize()) {
        char errBuf[128];
        snprintf(errBuf, sizeof(errBuf), "Content-Length %ld != erased size %lu",
                 static_cast<long>(contentLength),
                 static_cast<unsigned long>(_otaUpdater.imageSize()));
        Serial.printf("[OTA] %s\n", errBuf);
        sendHttpResponse(linkId, "400 Bad Request", "text/plain", errBuf);
        return;
    }

    _eraseDeadlineMs = 0;
    _updateState = UpdateState::RECEIVING;
    _updateLinkId = linkId;
    _updateBytesLeft = static_cast<uint32_t>(contentLength);
    _updateStartMs = millis();
    _updateLastDataMs = _updateStartMs;
    _uploadDiscPending = false;
    _lastProgressAt = 0;

    const char* bodyStart = strstr(request, "\r\n\r\n");
    if (bodyStart) {
        bodyStart += 4;
        int bodyBytes = reqLen - static_cast<int>(bodyStart - request);
        if (bodyBytes > 0) {
            handleUploadData(linkId, bodyStart, bodyBytes);
        }
    }
}

void ST67WifiOtaMode::handleUploadData(int linkId, const char* data, int len)
{
    if (_updateState != UpdateState::RECEIVING) {
        return;
    }

    uint32_t toWrite = (static_cast<uint32_t>(len) > _updateBytesLeft) ? _updateBytesLeft : static_cast<uint32_t>(len);

    if (!_otaUpdater.writeChunk(reinterpret_cast<const uint8_t*>(data), static_cast<size_t>(toWrite))) {
        Serial.printf("[OTA] Write failed: %s\n", _otaUpdater.lastError());
        _otaUpdater.abort();
        _updateState = UpdateState::IDLE;
        _updateLinkId = -1;
        _eraseDeadlineMs = 0;
        _uploadDiscPending = false;
        sendHttpResponse(linkId, "500 Internal Server Error", "text/plain", _otaUpdater.lastError());
        return;
    }

    _updateBytesLeft -= toWrite;
    uint32_t received = _otaUpdater.imageSize() - _updateBytesLeft;
    if ((received - _lastProgressAt >= 8192) || (_updateBytesLeft == 0)) {
        Serial.printf("[OTA] %lu / %lu bytes (%lu remaining)\n",
                      static_cast<unsigned long>(received),
                      static_cast<unsigned long>(_otaUpdater.imageSize()),
                      static_cast<unsigned long>(_updateBytesLeft));
        _lastProgressAt = received;
    }

    if (_updateBytesLeft == 0) {
        _updateState = UpdateState::IDLE;
        _updateLinkId = -1;
        _eraseDeadlineMs = 0;
        _uploadDiscPending = false;
        _lastProgressAt = 0;

        sendHttpResponse(linkId, "200 OK", "text/plain", "Update written and verified. Rebooting...");
        if (!_otaUpdater.finalize()) {
            Serial.printf("[OTA] Finalize failed: %s\n", _otaUpdater.lastError());
            return;
        }

        _pendingReboot = true;
        _rebootAt = millis() + 2000;
    }
}

// ============================================================================
// Request parsing utilities
// ============================================================================

bool ST67WifiOtaMode::looksLikeHttpRequest(const char* data, int len) const
{
    if (data == nullptr || len < 14) {
        return false;
    }

    const bool hasMethod =
        (len >= 4 && memcmp(data, "GET ", 4) == 0) ||
        (len >= 5 && memcmp(data, "POST ", 5) == 0) ||
        (len >= 5 && memcmp(data, "HEAD ", 5) == 0) ||
        (len >= 4 && memcmp(data, "PUT ", 4) == 0) ||
        (len >= 7 && memcmp(data, "DELETE ", 7) == 0) ||
        (len >= 8 && memcmp(data, "OPTIONS ", 8) == 0);

    if (!hasMethod) {
        return false;
    }

    const int scanLen = (len < 96) ? len : 96;
    for (int i = 0; i + 7 <= scanLen; ++i) {
        if (memcmp(data + i, "HTTP/1.", 7) == 0) {
            return true;
        }
    }
    return false;
}

int32_t ST67WifiOtaMode::parseContentLength(const char* request) const
{
    const char* headerNames[] = {"Content-Length:", "content-length:"};
    for (const char* name : headerNames) {
        const char* p = strstr(request, name);
        if (!p) continue;
        p += strlen(name);
        while (*p == ' ') p++;
        int32_t val = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
        }
        if (val > 0) return val;
    }
    return -1;
}

int32_t ST67WifiOtaMode::parseImageSize(const char* request) const
{
    const char* headerNames[] = {"X-Image-Size:", "x-image-size:"};
    for (const char* name : headerNames) {
        const char* p = strstr(request, name);
        if (!p) continue;
        p += strlen(name);
        while (*p == ' ') p++;
        int32_t val = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
        }
        if (val > 0) return val;
    }
    return -1;
}

void ST67WifiOtaMode::drainSpiFrames()
{
    uint8_t drain[512];
    int total = 0;
    uint32_t quietStart = millis();

    while (millis() - quietStart < 100) {
        if (_wifi.at.spi.isReady()) {
            int n = _wifi.at.spi.recvFrame(drain, sizeof(drain));
            if (n > 0) {
                total += n;
                quietStart = millis();
            }
        } else {
            delay(1);
        }
    }

    if (total > 0) {
        Serial.printf("[OTA] Drained %d stale bytes\n", total);
    }
}
