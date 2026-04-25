#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// STM32 web assets served by ST67WifiOtaMode::serveAsset()
//
// Routes:
//   GET /         -> /index.html  (device info page)
//   GET /update   -> /update.html (OTA firmware update)
//
// Assets are stored gzip-compressed in flash and sent with
// Content-Encoding: gzip so the browser decompresses them.
//
// To regenerate web-stm32-info.h after editing the HTML sources:
//   cd html && python3 build-stm32.py
// ---------------------------------------------------------------------------

typedef struct {
    const char*          path;
    const char*          content_type;
    const unsigned char* data;
    const size_t         size;
} WebAsset;

#include "web-stm32-info.h"

#endif // WEBPAGE_H
