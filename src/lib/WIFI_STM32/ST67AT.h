#ifndef ST67AT_H
#define ST67AT_H

#include <Arduino.h>
#include "ST67SPI.h"

// Buffer sizes
#define AT_RESP_BUF_SIZE    2048
#define AT_DEFAULT_TIMEOUT  5000
#define AT_INIT_TIMEOUT     15000
#define AT_SEND_TIMEOUT     15000
#define AT_MAX_SEND_CHUNK   4096    // max bytes per CIPSEND

// Stash for unsolicited reports that arrive during AT command execution.
// Typical reports (+IPD metadata, +CIP connect/disconnect, +CW events) are
// under 64 bytes.  Data frames (2KB+) are NOT stashed.
#define AT_STASH_SLOTS      4
#define AT_STASH_FRAME_MAX  64

/// Callback signature for unsolicited report messages.
/// The callee must NOT free the pointer.
typedef void (*ReportCallback)(const char* report, uint16_t len);

class ST67AT {
public:
    ST67SPI spi;

    /// Full start-up sequence: CHIP_EN, wait "ready", send AT, confirm OK.
    /// Returns true if module responded correctly.
    bool begin();

    /// Send an AT command string (without trailing \r\n — added automatically)
    /// and block until OK or ERROR or timeout.
    /// Returns true on OK.
    bool sendCommand(const char* cmd, uint32_t timeoutMs = AT_DEFAULT_TIMEOUT);

    /// Same as sendCommand but also copies the full response text into `response`.
    bool sendCommandGetResponse(const char* cmd, char* response, uint16_t maxLen,
                                uint32_t timeoutMs = AT_DEFAULT_TIMEOUT);

    /// Send CIPSEND payload after the ">" prompt using SPI framing.
    /// Returns true on "SEND OK".
    bool sendData(const uint8_t* data, uint16_t len, uint32_t timeoutMs = AT_SEND_TIMEOUT);

    /// Non-blocking: if the module has a pending report, read it into `report`.
    /// Returns number of bytes read, 0 if nothing available.
    int checkForReport(char* report, uint16_t maxLen);

    /// Install a callback that is invoked for every unsolicited report
    /// received while waiting for a command response.
    void onReport(ReportCallback cb) { _reportCb = cb; }

    /// Access the last raw response buffer (for debugging).
    const char* lastResponse() const { return _respBuf; }

private:
    char     _respBuf[AT_RESP_BUF_SIZE];
    uint16_t _respLen;

    ReportCallback _reportCb = nullptr;

    // Stash for unsolicited reports captured during command execution.
    struct StashedReport {
        char data[AT_STASH_FRAME_MAX];
        uint16_t len;
    };
    StashedReport _stash[AT_STASH_SLOTS];
    uint8_t       _stashCount = 0;

    /// Returns true if the frame looks like an unsolicited AT notification
    /// (starts with +IPD, +CIP, +CW) that should not be mixed into a
    /// command response buffer.
    static bool isUnsolicitedReport(const char* frame, int len);

    /// Save a small report frame for later retrieval by checkForReport().
    void stashReport(const char* frame, int len);

    /// Accumulate SPI frames into _respBuf until `okStr` or `errStr` is found.
    /// Unsolicited reports are stashed instead of appended.
    /// Returns true if okStr was found before timeout.
    bool collectUntil(const char* okStr, const char* errStr, uint32_t timeoutMs);

    /// Forward a report string to the callback (if installed).
    void forwardReport(const char* text, uint16_t len);
};

#endif // ST67AT_H
