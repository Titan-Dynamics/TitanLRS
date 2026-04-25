#ifndef ST67_WIFI_OTA_MODE_H
#define ST67_WIFI_OTA_MODE_H

#include <Arduino.h>
#include "ST67WiFi.h"
#include "OtaUpdater.h"

class ST67WifiOtaMode {
public:
    bool start();
    void service();
    bool isActive() const { return _active; }

private:
    enum class UpdateState : uint8_t {
        IDLE,
        RECEIVING,
    };

    ST67WiFi _wifi;
    OtaUpdater _otaUpdater;
    bool _active = false;

    // AP SSID derived from target's DEVICE_NAME; password matches ESP targets.
    static constexpr const char* AP_SSID = DEVICE_NAME;
    static constexpr const char* AP_PASSWORD = "expresslrs";
    static constexpr uint8_t AP_CHANNEL = 1;
    static constexpr uint8_t AP_ECN = 3;

    static constexpr uint32_t UPLOAD_STALL_TIMEOUT_MS = 15000;
    static constexpr uint32_t ERASE_TIMEOUT_MS = 120000;

    char _clientBuf[8192];

    UpdateState _updateState = UpdateState::IDLE;
    int _updateLinkId = -1;
    uint32_t _updateBytesLeft = 0;
    uint32_t _updateStartMs = 0;
    uint32_t _updateLastDataMs = 0;
    bool _pendingReboot = false;
    uint32_t _rebootAt = 0;
    bool _uploadDiscPending = false;
    uint32_t _eraseDeadlineMs = 0;
    uint32_t _lastProgressAt = 0;

    void handleClientRequest(int linkId, const char* request, int reqLen);
    void sendHttpResponse(int linkId, const char* status,
                          const char* contentType, const char* body,
                          bool closeConn = true);
    void serveAsset(int linkId, const char* path);
    void handleGetConfig(int linkId);
    void handlePostErase(int linkId, const char* request, int reqLen);
    void handlePostUpload(int linkId, const char* request, int reqLen);
    void handleUploadData(int linkId, const char* data, int len);
    void drainSpiFrames();
    bool looksLikeHttpRequest(const char* data, int len) const;
    int32_t parseContentLength(const char* request) const;
    int32_t parseImageSize(const char* request) const;
};

#endif // ST67_WIFI_OTA_MODE_H
