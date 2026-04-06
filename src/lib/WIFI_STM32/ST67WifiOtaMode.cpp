#include "ST67WifiOtaMode.h"

#include <cstring>
#include <stm32h7xx_hal.h>

#include "WebPage.h"

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

int32_t ST67WifiOtaMode::parseContentLength(const char* request) const
{
    const char* headerNames[] = {"Content-Length:", "content-length:"};
    for (const char* name : headerNames) {
        const char* p = strstr(request, name);
        if (!p) {
            continue;
        }
        p += strlen(name);
        while (*p == ' ') {
            p++;
        }
        int32_t val = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
        }
        if (val > 0) {
            return val;
        }
    }
    return -1;
}

int32_t ST67WifiOtaMode::parseImageSize(const char* request) const
{
    const char* headerNames[] = {"X-Image-Size:", "x-image-size:"};
    for (const char* name : headerNames) {
        const char* p = strstr(request, name);
        if (!p) {
            continue;
        }
        p += strlen(name);
        while (*p == ' ') {
            p++;
        }
        int32_t val = 0;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
        }
        if (val > 0) {
            return val;
        }
    }
    return -1;
}

void ST67WifiOtaMode::handleClientRequest(int linkId, const char* request, int reqLen)
{
    const char* eol = strchr(request, '\r');
    if (eol) {
        char line[128];
        int lineLen = static_cast<int>(eol - request);
        if (lineLen > static_cast<int>(sizeof(line) - 1)) {
            lineLen = sizeof(line) - 1;
        }
        memcpy(line, request, lineLen);
        line[lineLen] = '\0';
        Serial.printf("[HTTP] %s\n", line);
    }

    bool isGetRoot = (strstr(request, "GET / ") != nullptr) ||
                     (strstr(request, "GET /index") != nullptr);
    bool isPostErase = (strstr(request, "POST /erase") != nullptr);
    bool isPostUpload = (strstr(request, "POST /upload") != nullptr);

    if (isPostErase) {
        handlePostErase(linkId, request, reqLen);
    } else if (isPostUpload) {
        handlePostUpload(linkId, request, reqLen);
    } else if (isGetRoot) {
        handleGetRoot(linkId);
    } else {
        sendHttpResponse(linkId, "404 Not Found", "text/plain", "404 - Not Found");
    }
}

void ST67WifiOtaMode::handleGetRoot(int linkId)
{
    uint16_t bodyLen = static_cast<uint16_t>(strlen(HTML_BODY));

    char hdr[256];
    int hdrLen = snprintf(hdr, sizeof(hdr),
                          "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/html; charset=UTF-8\r\n"
                          "Content-Length: %u\r\n"
                          "Connection: close\r\n"
                          "\r\n",
                          bodyLen);

    _wifi.sendResponse(linkId, reinterpret_cast<const uint8_t*>(hdr), static_cast<uint16_t>(hdrLen));
    _wifi.sendResponse(linkId, reinterpret_cast<const uint8_t*>(HTML_BODY), bodyLen);

    delay(50);
    _wifi.closeConnection(linkId);
}

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
