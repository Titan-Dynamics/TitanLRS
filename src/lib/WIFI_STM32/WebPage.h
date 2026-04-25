#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// STM32 web assets served by ST67WifiOtaMode::serveAsset()
//
// Static page routes:
//   GET /         -> /index.html   (device info / overview page)
//   GET /binding  -> /binding.html (binding UID configuration)
//   GET /update   -> /update.html  (OTA firmware update)
//   GET /options  -> /options.html (firmware options / WiFi settings)
//   GET /wifi     -> /wifi.html    (home network configuration)
//   GET /buttons  -> /buttons.html (button LED config, TX only)
//   GET /import   -> /import.html  (import/export config, TX only)
//
// API routes (no asset needed):
//   GET  /config            -> GET /config JSON response
//   GET  /config?export     -> GET /config JSON (extended, TX only)
//   POST /config            -> update runtime config (EEPROM)
//   GET  /options.json      -> firmware options JSON
//   POST /options.json      -> update firmware options
//   GET  /reboot            -> trigger device reboot
//   GET  /reset?options&model -> factory reset
//   GET  /networks.json     -> WiFi network scan (placeholder, returns [])
//   POST /sethome           -> save home WiFi credentials
//   POST /forget            -> clear home WiFi credentials
//   POST /erase             -> OTA: erase inactive flash bank
//   POST /upload            -> OTA: stream firmware binary
//   POST /import            -> import full config (TX only)
//   POST /buttons           -> preview button LED colors (TX only)
//
// Assets are stored gzip-compressed in flash and sent with
// Content-Encoding: gzip so the browser decompresses them.
//
// To regenerate web-stm32-info.h after editing the HTML sources:
//   cd html && node build-stm32.js
// ---------------------------------------------------------------------------

typedef struct {
    const char*          path;
    const char*          content_type;
    const unsigned char* data;
    const size_t         size;
} WebAsset;

#include "web-stm32-info.h"

#endif // WEBPAGE_H
