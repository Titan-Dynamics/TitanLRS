#include "ST67AT.h"

// Pin defines come from target headers via -include (no pins.h needed).
// Uses: GPIO_PIN_ST67_BOOT, GPIO_PIN_ST67_CHIP_EN, GPIO_PIN_ST67_SCK,
//       GPIO_PIN_ST67_MISO, GPIO_PIN_ST67_MOSI, GPIO_PIN_ST67_CS,
//       GPIO_PIN_ST67_RDY, GPIO_PIN_ST67_SPI_FREQ

// ---------------------------------------------------------------------------
// begin — full startup handshake
// ---------------------------------------------------------------------------
bool ST67AT::begin()
{
    // 1. Configure control pins
    pinMode(GPIO_PIN_ST67_BOOT, OUTPUT);
    pinMode(GPIO_PIN_ST67_CHIP_EN, OUTPUT);

    // 2. BOOT LOW = normal mission mode (not flashing)
    digitalWrite(GPIO_PIN_ST67_BOOT, LOW);

    // SPI pin mapping from target header.
    spi.begin(GPIO_PIN_ST67_SCK, GPIO_PIN_ST67_MISO, GPIO_PIN_ST67_MOSI,
              GPIO_PIN_ST67_CS, GPIO_PIN_ST67_RDY, GPIO_PIN_ST67_SPI_FREQ);

    const uint8_t maxBootAttempts = 2;

    for (uint8_t bootTry = 1; bootTry <= maxBootAttempts; bootTry++) {
        Serial.printf("[AT] Boot attempt %u/%u\n", bootTry, maxBootAttempts);

        // 3. Power-cycle the module on each attempt.
        digitalWrite(GPIO_PIN_ST67_CHIP_EN, LOW);
        delay(150);
        digitalWrite(GPIO_PIN_ST67_CHIP_EN, HIGH);

        Serial.println(F("[AT] CHIP_EN HIGH — waiting for module boot..."));

        // 4. Wait for SPI_RDY (module boot takes several seconds)
        if (!spi.waitReady(SPI_BOOT_TIMEOUT_MS)) {
            Serial.println(F("[AT] ERROR: SPI_RDY never asserted after boot"));
            continue;
        }

        // 5. Read the "ready" report (retry a few times if needed)
        _respLen = 0;
        memset(_respBuf, 0, sizeof(_respBuf));
        int n = -1;
        for (int attempt = 0; attempt < 5; attempt++) {
            n = spi.recvFrame((uint8_t*)_respBuf, sizeof(_respBuf) - 1);
            Serial.printf("[AT] recvFrame attempt %d returned: %d\n", attempt, n);
            if (n > 0) {
                break;
            }
            delay(150);
            if (!spi.waitReady(2000)) {
                Serial.println(F("[AT] RDY not asserted for retry"));
                break;
            }
        }

        if (n > 0) {
            _respBuf[n] = '\0';
            Serial.printf("[AT] Boot msg (%d bytes): %s\n", n, _respBuf);
        } else {
            Serial.println(F("[AT] No valid boot frame received"));
        }

        if (strstr(_respBuf, "ready") == nullptr) {
            Serial.println(F("[AT] WARNING: 'ready' not found in boot message"));
            // Continue anyway — some firmware versions behave differently.
        }

        // 6. Send bare "AT" to verify comms
        delay(100);
        if (sendCommand("AT", AT_INIT_TIMEOUT)) {
            Serial.println(F("[AT] Module is alive — AT->OK confirmed"));

            // 7. Factory-reset stored parameters so stale AP config from
            //    previous firmware doesn't override our configuration.
            Serial.println(F("[AT] Restoring factory defaults..."));
            sendCommand("AT+RESTORE", 5000);

            // AT+RESTORE triggers a module reboot — wait for it to come back.
            delay(500);
            if (!spi.waitReady(SPI_BOOT_TIMEOUT_MS)) {
                Serial.println(F("[AT] WARNING: SPI_RDY not asserted after RESTORE"));
                return true; // module was alive before RESTORE, proceed anyway
            }

            // Drain the post-reboot "ready" frame
            _respLen = 0;
            memset(_respBuf, 0, sizeof(_respBuf));
            for (int attempt = 0; attempt < 5; attempt++) {
                int nr = spi.recvFrame((uint8_t*)_respBuf, sizeof(_respBuf) - 1);
                if (nr > 0) {
                    _respBuf[nr] = '\0';
                    Serial.printf("[AT] Post-restore boot msg: %s\n", _respBuf);
                    break;
                }
                delay(150);
                if (!spi.waitReady(2000)) break;
            }

            // Re-confirm AT after factory reset
            delay(100);
            if (sendCommand("AT", AT_INIT_TIMEOUT)) {
                Serial.println(F("[AT] Module confirmed after factory reset"));
                return true;
            }

            Serial.println(F("[AT] WARNING: No AT response after RESTORE, proceeding anyway"));
            return true;
        }

        Serial.println(F("[AT] ERROR: No OK response to AT command"));
        if (bootTry < maxBootAttempts) {
            Serial.println(F("[AT] Retrying startup with a full power-cycle..."));
        }
    }

    Serial.println(F("[AT] DIAG: Startup failed after retries. Check wiring, power, and mission firmware mode."));
    return false;
}

// ---------------------------------------------------------------------------
// sendCommand — send AT string, wait for OK / ERROR
// ---------------------------------------------------------------------------
bool ST67AT::sendCommand(const char* cmd, uint32_t timeoutMs)
{
    return sendCommandGetResponse(cmd, nullptr, 0, timeoutMs);
}

bool ST67AT::sendCommandGetResponse(const char* cmd, char* response,
                                    uint16_t maxLen, uint32_t timeoutMs)
{
    if (cmd == nullptr) {
        return false;
    }

    // Build the full command with trailing \r\n
    const uint16_t cmdLen = (uint16_t)strlen(cmd);
    if (cmdLen > 250u) {
        Serial.println(F("[AT] command too long"));
        return false;
    }
    const uint16_t frameLen = cmdLen + 2u;             // +\r\n
    uint8_t frameBuf[252];
    memcpy(frameBuf, cmd, cmdLen);
    frameBuf[cmdLen]     = '\r';
    frameBuf[cmdLen + 1] = '\n';

    Serial.printf("[AT] >>> %s\n", cmd);

    // Clear response buffer
    _respLen = 0;
    memset(_respBuf, 0, sizeof(_respBuf));

    // Send framed command
    int rc = spi.sendFrame(frameBuf, frameLen);
    if (rc < 0) {
        Serial.printf("[AT] sendFrame failed: %d\n", rc);
        return false;
    }

    // Collect responses until OK or ERROR
    bool ok = collectUntil("\r\nOK\r\n", "ERROR", timeoutMs);

    // Copy to caller buffer if requested
    if (response && maxLen > 0) {
        uint16_t copyLen = (_respLen < maxLen - 1) ? _respLen : (maxLen - 1);
        memcpy(response, _respBuf, copyLen);
        response[copyLen] = '\0';
    }

    Serial.printf("[AT] <<< [%s] %s\n", ok ? "OK" : "FAIL", _respBuf);
    return ok;
}

// ---------------------------------------------------------------------------
// sendData — CIPSEND payload phase over SPI-framed transport
// ---------------------------------------------------------------------------
bool ST67AT::sendData(const uint8_t* data, uint16_t len, uint32_t timeoutMs)
{
    // ST67 SPI transport remains framed even in CIPSEND data mode.
    // The module expects the payload as a normal SPI frame whose data
    // length matches the CIPSEND length argument.
    int rc = spi.sendFrame(data, len);
    if (rc < 0) {
        Serial.printf("[AT] sendFrame(data) failed: %d\n", rc);
        return false;
    }

    // Wait for SEND OK / SEND FAIL
    _respLen = 0;
    memset(_respBuf, 0, sizeof(_respBuf));
    bool ok = collectUntil("SEND OK", "SEND FAIL", timeoutMs);
    if (ok) {
        Serial.println(F("[AT] <<< data result: SEND OK"));
    } else if (strstr(_respBuf, "SEND FAIL")) {
        Serial.println(F("[AT] <<< data result: SEND FAIL"));
    } else {
        Serial.printf("[AT] <<< data result: TIMEOUT (buf='%s')\n", _respBuf);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// checkForReport — non-blocking poll for unsolicited reports
// ---------------------------------------------------------------------------
int ST67AT::checkForReport(char* report, uint16_t maxLen)
{
    if (report == nullptr || maxLen < 2u) {
        return 0;
    }

    // Drain stashed reports first (captured during command execution).
    if (_stashCount > 0) {
        StashedReport& front = _stash[0];
        uint16_t copyLen = (front.len < maxLen - 1) ? front.len : (maxLen - 1);
        memcpy(report, front.data, copyLen);
        report[copyLen] = '\0';

        // Shift remaining entries forward
        _stashCount--;
        for (uint8_t i = 0; i < _stashCount; i++) {
            _stash[i] = _stash[i + 1];
        }

        forwardReport(report, copyLen);
        return (int)copyLen;
    }

    if (!spi.isReady()) return 0;

    int n = spi.recvFrame((uint8_t*)report, maxLen - 1);
    if (n > 0) {
        report[n] = '\0';
        forwardReport(report, (uint16_t)n);
    }
    return (n > 0) ? n : 0;
}

// ---------------------------------------------------------------------------
// isUnsolicitedReport — detect +IPD, +CIP, +CW notifications
// ---------------------------------------------------------------------------
bool ST67AT::isUnsolicitedReport(const char* frame, int len)
{
    if (len < 4 || frame[0] != '+') return false;

    // Match known unsolicited report prefixes.
    // +IPD — incoming TCP data notification
    // +CIP — connection state (CONNECTED / DISCONNECTED)
    // +CW  — WiFi events (STA_CONNECTED, DIST_STA_IP, etc.)
    return (memcmp(frame, "+IPD", 4) == 0) ||
           (memcmp(frame, "+CIP", 4) == 0) ||
           (memcmp(frame, "+CW:",  4) == 0 || memcmp(frame, "+CW,", 4) == 0);
}

// ---------------------------------------------------------------------------
// stashReport — save a small report for later retrieval
// ---------------------------------------------------------------------------
void ST67AT::stashReport(const char* frame, int len)
{
    if (_stashCount >= AT_STASH_SLOTS) {
        Serial.println(F("[AT] WARNING: stash full, dropping report"));
        return;
    }
    if (len > AT_STASH_FRAME_MAX) {
        Serial.printf("[AT] WARNING: report too large to stash (%d bytes)\n", len);
        return;
    }

    StashedReport& slot = _stash[_stashCount];
    memcpy(slot.data, frame, len);
    slot.len = (uint16_t)len;
    _stashCount++;
}

// ---------------------------------------------------------------------------
// collectUntil — accumulate frames until a target string appears
// ---------------------------------------------------------------------------
bool ST67AT::collectUntil(const char* okStr, const char* errStr,
                          uint32_t timeoutMs)
{
    uint32_t t0 = millis();

    while (millis() - t0 < timeoutMs) {
        // Wait for module to have data
        if (!spi.isReady()) {
            delay(1);
            continue;
        }

        // Receive one frame into the tail of _respBuf
        uint16_t space = sizeof(_respBuf) - _respLen - 1;
        if (space == 0) break;  // buffer full

        uint16_t prevLen = _respLen;
        int n = spi.recvFrame((uint8_t*)_respBuf + _respLen, space);
        if (n <= 0) continue;

        // Check if this frame is an unsolicited report that arrived
        // mid-command.  Stash it instead of keeping it in the response
        // buffer so checkForReport() can deliver it later.
        char* frameStart = _respBuf + prevLen;
        if (isUnsolicitedReport(frameStart, n) && n <= AT_STASH_FRAME_MAX) {
            stashReport(frameStart, n);
            forwardReport(frameStart, (uint16_t)n);
            // Don't advance _respLen — frame is removed from response buffer.
            _respBuf[prevLen] = '\0';
            continue;
        }

        _respLen += n;
        _respBuf[_respLen] = '\0';

        // Check for success
        if (strstr(_respBuf, okStr)) return true;

        // Check for failure
        if (errStr && strstr(_respBuf, errStr)) return false;

        // "busy p" means keep waiting
    }

    return false;   // timeout
}

void ST67AT::forwardReport(const char* text, uint16_t len)
{
    if (_reportCb) _reportCb(text, len);
}
