#include "ST67WiFi.h"
#include <cstring>

// ---------------------------------------------------------------------------
// begin
// ---------------------------------------------------------------------------
bool ST67WiFi::begin()
{
    return at.begin();
}

// ---------------------------------------------------------------------------
// Phase 1 — Soft-AP
// ---------------------------------------------------------------------------

bool ST67WiFi::startAP(const char* ssid, const char* password,
                       uint8_t channel, uint8_t ecn)
{
    // 1. Enable Wi-Fi radio
    if (!at.sendCommand("AT+WIFISP=1")) {
        Serial.println(F("[WiFi] WIFISP failed"));
        return false;
    }

    // 2. Set SoftAP mode
    if (!at.sendCommand("AT+CWMODE=2")) {
        Serial.println(F("[WiFi] CWMODE failed"));
        return false;
    }

    // 3. Configure AcessPoint parameters
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "AT+CWSAP=\"%s\",\"%s\",%u,%u,4,0",
             ssid, password, channel, ecn);
    if (!at.sendCommand(cmd, 10000)) {
        Serial.println(F("[WiFi] CWSAP failed"));
        return false;
    }

    Serial.printf("[WiFi] AP started: SSID=%s  CH=%u  ECN=%u\n",
                  ssid, channel, ecn);
    return true;
}

bool ST67WiFi::startDHCP()
{
    // Enable SoftAP DHCP: operate=1 (enable), mode=2 (bit1 = SoftAP DHCP)
    if (!at.sendCommand("AT+CWDHCP=1,2")) {
        Serial.println(F("[WiFi] CWDHCP failed"));
        return false;
    }
    Serial.println(F("[WiFi] DHCP server enabled"));
    return true;
}

bool ST67WiFi::getAPIP(char* ipOut, uint16_t maxLen)
{
    char resp[256];
    if (!at.sendCommandGetResponse("AT+CIPAP?", resp, sizeof(resp))) {
        Serial.println(F("[WiFi] CIPAP? failed"));
        return false;
    }

    // Response format:
    //   +CIPAP:ip:"10.19.96.1"
    //   +CIPAP:gateway:"10.19.96.1"
    //   +CIPAP:netmask:"255.255.255.0"
    //   OK
    const char* p = strstr(resp, "+CIPAP:ip:\"");
    if (!p) {
        // Try alternate format: +CIPAP:ip:<ip>
        p = strstr(resp, "+CIPAP:ip:");
        if (!p) {
            ipOut[0] = '\0';
            return false;
        }
        p += 10; // skip "+CIPAP:ip:"
    } else {
        p += 11; // skip "+CIPAP:ip:\""
    }

    // Copy until quote or \r or end
    uint16_t i = 0;
    while (*p && *p != '"' && *p != '\r' && *p != '\n' && i < maxLen - 1) {
        ipOut[i++] = *p++;
    }
    ipOut[i] = '\0';
    return i > 0;
}

// ---------------------------------------------------------------------------
// Phase 2 — TCP Server
// ---------------------------------------------------------------------------

bool ST67WiFi::startTCPServer(uint16_t port)
{
    // Enable multiple connections (required for CIPSERVER)
    if (!at.sendCommand("AT+CIPMUX=1")) {
        Serial.println(F("[WiFi] CIPMUX failed"));
        return false;
    }

    // Create TCP server
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CIPSERVER=1,%u", port);
    if (!at.sendCommand(cmd)) {
        Serial.println(F("[WiFi] CIPSERVER failed"));
        return false;
    }

    // Set server timeout (seconds)
    at.sendCommand("AT+CIPSTO=30");

    Serial.printf("[WiFi] TCP server listening on port %u\n", port);
    return true;
}

// ---------------------------------------------------------------------------
// checkForClient — poll for +IPD reports
// ---------------------------------------------------------------------------
int ST67WiFi::checkForClient(char* buf, uint16_t maxLen, int* linkId)
{
    if (buf == nullptr || maxLen < 2u || linkId == nullptr) {
        return 0;
    }

    *linkId = -1;

    int n = at.checkForReport(_reportBuf, sizeof(_reportBuf));
    if (n <= 0) return 0;

    const bool isIpdPrefix = (n >= 5) &&
        (memcmp(_reportBuf, "+IPD:", 5) == 0 ||
         memcmp(_reportBuf, "+IPD,", 5) == 0);
    const bool isHttpText = ((n >= 4) && (memcmp(_reportBuf, "GET ", 4) == 0)) ||
                            ((n >= 5) && (memcmp(_reportBuf, "POST ", 5) == 0)) ||
                            ((n >= 5) && (memcmp(_reportBuf, "HTTP/", 5) == 0));
    const bool isLikelyBinary = (n > 256) && !isIpdPrefix && !isHttpText && (_reportBuf[0] != '+');
    const bool isSmallIpdMeta = isIpdPrefix && (n <= 16);

    // Suppress very chatty upload stream logs to avoid serial backpressure.
    static uint32_t suppressedStreamLogs = 0;
    if (isLikelyBinary || isSmallIpdMeta) {
        suppressedStreamLogs++;
        if ((suppressedStreamLogs % 256u) == 0u) {
            Serial.printf("[WiFi] Upload stream active (%lu frames suppressed)\n",
                          (unsigned long)suppressedStreamLogs);
        }
    } else if (n > 256) {
        if (isIpdPrefix) {
            Serial.printf("[WiFi] Report (%d bytes): +IPD...\n", n);
        } else if (isHttpText) {
            Serial.printf("[WiFi] Report (%d bytes): %.40s...\n", n, _reportBuf);
        } else {
            Serial.printf("[WiFi] Report (%d bytes): <binary>\n", n);
        }
    } else {
        Serial.printf("[WiFi] Report (%d bytes): %.80s%s\n",
                      n, _reportBuf, n > 80 ? "..." : "");
    }

    // Some ST67 firmware versions send +IPD metadata in one frame and raw
    // payload in the next frame(s). If an IPD payload is pending, check
    // whether this frame is raw continuation data or a concurrent AT report.
    if (_pendingIpdLen > 0 && _pendingIpdLink >= 0) {
        // Detect notifications that can arrive mid-transfer.
        const bool isIpdMeta = (n >= 5) &&
            (memcmp(_reportBuf, "+IPD:", 5) == 0 ||
             memcmp(_reportBuf, "+IPD,", 5) == 0);
        const bool isCipEvent = (n >= 5) && (memcmp(_reportBuf, "+CIP:", 5) == 0);
        const bool isCwEvent  = (n >= 4) && (memcmp(_reportBuf, "+CW:", 4) == 0);
        const bool isNewNotification = isIpdMeta || isCipEvent || isCwEvent;

        if (!isNewNotification) {
            // Raw continuation data — attribute to the pending link.
            *linkId = _pendingIpdLink;
            uint16_t payloadLen = (n < _pendingIpdLen) ? (uint16_t)n : (uint16_t)_pendingIpdLen;
            uint16_t copyLen = (payloadLen < (maxLen - 1)) ? payloadLen : (maxLen - 1);

            memcpy(buf, _reportBuf, copyLen);
            buf[copyLen] = '\0';

            _pendingIpdLen -= payloadLen;
            if (_pendingIpdLen <= 0) {
                _pendingIpdLen = 0;
                _pendingIpdLink = -1;
            }

            return (int)copyLen;
        }

        // +CIP/+CW can be interleaved with data delivery. Preserve pending
        // payload state so following raw frames are still consumed as upload
        // data; return this report as a non-payload event to caller.
        if (!isIpdMeta) {
            uint16_t copyLen = (n < (int)(maxLen - 1)) ? n : (maxLen - 1);
            memcpy(buf, _reportBuf, copyLen);
            buf[copyLen] = '\0';
            return 0;
        }

        // A new +IPD metadata frame arrived while pending bytes remained.
        // Re-sync by dropping the old pending metadata and parsing this +IPD.
        _pendingIpdLen  = 0;
        _pendingIpdLink = -1;
    }

    // Look for +IPD metadata. Accept both "+IPD," and "+IPD:" forms.
    const char* ipd = strstr(_reportBuf, "+IPD:");
    uint8_t ipdPrefixLen = 5;
    if (!ipd) {
        ipd = strstr(_reportBuf, "+IPD,");
    }
    if (!ipd) {
        // Not a data report — might be CONNECTED / DISCONNECTED etc.
        // Copy to caller anyway so they can inspect
        uint16_t copyLen = (n < (int)(maxLen - 1)) ? n : (maxLen - 1);
        memcpy(buf, _reportBuf, copyLen);
        buf[copyLen] = '\0';
        return 0;   // no IPD payload
    }

    // Parse link_id
    ipd += ipdPrefixLen;   // skip "+IPD:" or "+IPD,"
    int lid = 0;
    while (*ipd >= '0' && *ipd <= '9') {
        lid = lid * 10 + (*ipd - '0');
        ipd++;
    }
    *linkId = lid;

    if (*ipd == ',') ipd++;     // skip comma after link id

    // Parse payload length
    int plen = 0;
    while (*ipd >= '0' && *ipd <= '9') {
        plen = plen * 10 + (*ipd - '0');
        ipd++;
    }

    // Delimiter before payload can be ':' or ','.
    if (*ipd == ':' || *ipd == ',') ipd++;

    // Copy payload data to caller buffer
    uint16_t available = (uint16_t)(_reportBuf + n - ipd);
    if (available > 0) {
        uint16_t payloadLen = (available < (uint16_t)plen) ? available : (uint16_t)plen;
        uint16_t copyLen = (payloadLen < (maxLen - 1)) ? payloadLen : (maxLen - 1);
        memcpy(buf, ipd, copyLen);
        buf[copyLen] = '\0';

        if ((int)payloadLen < plen) {
            _pendingIpdLink = lid;
            _pendingIpdLen = plen - (int)payloadLen;
        } else {
            _pendingIpdLink = -1;
            _pendingIpdLen = 0;
        }

        return (int)copyLen;
    }

    // Metadata-only frame (for example: "+IPD:0,438,").
    _pendingIpdLink = lid;
    _pendingIpdLen = plen;
    return 0;
}

// ---------------------------------------------------------------------------
// sendResponse — AT+CIPSEND flow
// ---------------------------------------------------------------------------
bool ST67WiFi::sendResponse(int linkId, const uint8_t* data, uint16_t len)
{
    uint16_t sent = 0;
    while (sent < len) {
        uint16_t chunk = len - sent;
        if (chunk > AT_MAX_SEND_CHUNK) chunk = AT_MAX_SEND_CHUNK;
        if (!sendChunk(linkId, data + sent, chunk)) return false;
        sent += chunk;
    }
    return true;
}

bool ST67WiFi::sendChunk(int linkId, const uint8_t* data, uint16_t len)
{
    if (data == nullptr || len == 0u) {
        return false;
    }

    // 1. Issue AT+CIPSEND=<linkId>,<len>
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%u", linkId, len);

    // Build framed command manually so we can detect ">"
    const uint16_t cmdLen = (uint16_t)strlen(cmd);
    uint8_t frameBuf[50];
    memcpy(frameBuf, cmd, cmdLen);
    frameBuf[cmdLen]     = '\r';
    frameBuf[cmdLen + 1] = '\n';

    Serial.printf("[WiFi] >>> %s\n", cmd);

    int rc = at.spi.sendFrame(frameBuf, cmdLen + 2);
    if (rc < 0) {
        Serial.println(F("[WiFi] CIPSEND sendFrame failed"));
        return false;
    }

    // 2. Collect frames until we see ">"
    char resp[256];
    uint16_t respLen = 0;
    memset(resp, 0, sizeof(resp));
    uint32_t t0 = millis();

    while (millis() - t0 < AT_DEFAULT_TIMEOUT) {
        if (!at.spi.isReady()) { delay(1); continue; }

        uint16_t space = sizeof(resp) - respLen - 1;
        if (space == 0) break;

        int n = at.spi.recvFrame((uint8_t*)resp + respLen, space);
        if (n <= 0) continue;
        respLen += n;
        resp[respLen] = '\0';

        if (strchr(resp, '>')) break;           // ready for data
        if (strstr(resp, "ERROR")) {
            Serial.printf("[WiFi] CIPSEND rejected: %s\n", resp);
            return false;
        }
    }

    if (!strchr(resp, '>')) {
        Serial.println(F("[WiFi] CIPSEND: never got '>' prompt"));
        return false;
    }

    // 3. Send payload data (framed SPI transport)
    return at.sendData(data, len);
}

// ---------------------------------------------------------------------------
// closeConnection
// ---------------------------------------------------------------------------
bool ST67WiFi::closeConnection(int linkId)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d", linkId);
    // Don't fail hard if close doesn't get OK — connection may already be gone
    at.sendCommand(cmd, 3000);
    return true;
}
