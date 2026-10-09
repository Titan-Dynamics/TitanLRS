#pragma once

/*
 * TitanLRS USB config API — HTTP over the USB network interface
 * ==============================================================
 *
 * The STM32 targets enumerate a CDC-NCM network adapter next to the CDC serial port
 * (lib/USBComposite). The device is 10.73.1.1 on a TX and 10.73.2.1 on an RX (lib/USBNet), and
 * serves this API on port 80 (devUSBConfig.cpp). Shapes follow the ESP web server
 * (lib/WIFI/devWIFI.cpp) wherever there is an equivalent.
 *
 *   GET  /hello                    identity + `features` bitmask (below) — what the client probes
 *   GET  /config[?export]          ConfigJson_BuildConfig
 *   POST /config, POST /import     ConfigJson_ApplyConfig (refused while armed)
 *   GET  /options.json             ConfigJson_BuildOptions
 *   POST /options.json             ConfigJson_ApplyOptions — reboot to apply (refused while armed)
 *   GET  /hardware.json            the effective hardware layout (unified targets)
 *   POST /hardware.json            save a layout override — reboot to apply (HARDWARE_WRITE)
 *   POST /reboot                   reboot ~100 ms after the response
 *   POST /reset?config&options&hardware   reset what is named, then reboot
 *   POST /dfu                      reboot into the ROM DFU bootloader (DFU)
 *   POST /crsf                     body: one or more raw CRSF frames, into the CRSF router
 *   GET  /crsf?wait=<ms>           long-poll: raw CRSF frames for the browser, or 204 on timeout
 *
 * Errors are an HTTP status with a JSON body {"error": "..."}: 400 bad request / JSON, 404 not
 * available on this build or without a hardware layout, 409 module armed, 413 too large,
 * 500 apply failed.
 *
 * Every response carries CORS headers and `Access-Control-Allow-Private-Network: true`: the
 * client is the hosted web flasher, a public HTTPS page reaching a private address through
 * Chrome's Local Network Access.
 */

#include <stdint.h>

// Bumped when an incompatible change is made to the routes or document shapes.
#define USBCFG_API_VERSION 2

// ---- /hello feature bitmask ------------------------------------------------------------
#define TLRS_FEATURE_OPTIONS_WRITE  (1u << 0) // POST /options.json supported and persisted
#define TLRS_FEATURE_CW             (1u << 1) // continuous-wave control (reserved)
#define TLRS_FEATURE_LR1121_UPDATE  (1u << 2) // LR1121 firmware upload (reserved)
#define TLRS_FEATURE_CRSF_PARAMS    (1u << 3) // /crsf tunnel to the CRSF parameter tree
#define TLRS_FEATURE_DFU            (1u << 4) // POST /dfu reboots into the MCU's ROM DFU bootloader
#define TLRS_FEATURE_HARDWARE_WRITE (1u << 5) // POST /hardware.json / reset?hardware persist an override
