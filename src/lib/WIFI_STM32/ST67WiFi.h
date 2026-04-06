#ifndef ST67WIFI_H
#define ST67WIFI_H

#include <Arduino.h>
#include "ST67AT.h"

#define WIFI_IP_BUF_SIZE    32
#define WIFI_REPORT_BUF     8192

class ST67WiFi {
public:
    ST67AT at;

    /// Full initialisation: SPI + AT boot handshake.
    bool begin();

    // ------ Phase 1: Soft-AP ------

    /// Configure and start a Wi-Fi Soft-AP.
    /// @param ecn  Encryption: 0=OPEN, 2=WPA, 3=WPA2, 4=WPA3.
    bool startAP(const char* ssid, const char* password,
                 uint8_t channel = 1, uint8_t ecn = 3);

    /// Enable the DHCP server on the Soft-AP interface.
    bool startDHCP();

    /// Query the Soft-AP IP address.  Stores result in ipOut (null-terminated).
    bool getAPIP(char* ipOut, uint16_t maxLen);

    // ------ Phase 2: TCP server ------

    /// Enable multiple TCP connections and create a TCP server on `port`.
    bool startTCPServer(uint16_t port = 80);

    /// Non-blocking check for an incoming +IPD report.
    /// On success, fills `buf` with the raw report text,
    /// sets `linkId` to the connection id, and returns the payload length.
    /// Returns 0 when nothing is available.
    int  checkForClient(char* buf, uint16_t maxLen, int* linkId);

    /// Send an HTTP response to the specified link.
    /// Handles CIPSEND flow:  command → ">" → raw data → SEND OK.
    /// Automatically chunks if `len` > AT_MAX_SEND_CHUNK.
    bool sendResponse(int linkId, const uint8_t* data, uint16_t len);

    /// Close a TCP connection.
    bool closeConnection(int linkId);

private:
    char _reportBuf[WIFI_REPORT_BUF];
    int  _pendingIpdLink = -1;
    int  _pendingIpdLen  = 0;

    /// Helper: send AT+CIPSEND for one chunk.
    bool sendChunk(int linkId, const uint8_t* data, uint16_t len);
};

#endif // ST67WIFI_H
