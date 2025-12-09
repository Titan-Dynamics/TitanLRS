#include "targets.h"

#if defined(PLATFORM_ESP32) && defined(OPT_USE_TX_BACKPACK)

#include "BackpackCRSF.h"
#include "CRSFRouter.h"
#include "logging.h"

// BackpackOrLogStrm is defined in devBackpack.cpp
extern Stream *BackpackOrLogStrm;

// Global instance
BackpackCRSF backpackCRSF;

BackpackCRSF::BackpackCRSF() {
}

void BackpackCRSF::begin() {
    // Register the backpack address as reachable via this connector
    addDevice(CRSF_ADDRESS_ELRS_LUA);
    addDevice(CRSF_ADDRESS_RADIO_TRANSMITTER);
    
    // Register this connector with the global CRSF router
    crsfRouter.addConnector(this);
    
    DBGLN("Backpack CRSF connector initialized");
}

void BackpackCRSF::processReceivedByte(uint8_t byte) {
    // Process the byte through the CRSF parser
    // When a complete frame is received, it will be passed to the router
    parser.processByte(this, byte, [](const crsf_header_t *message) {
        // Forward complete CRSF frames to the router for processing
        crsfRouter.processMessage(&backpackCRSF, message);
    });
}

void BackpackCRSF::forwardMessage(const crsf_header_t *message) {
    if (!BackpackOrLogStrm) {
        return;
    }
    
    // Calculate total frame size: sync + length + payload + crc
    // Length byte contains the count of bytes after it (type + payload + crc)
    uint8_t frameSize = message->frame_size + 2; // +2 for sync and length bytes
    
    DBGVLN("Backpack CRSF TX: type=0x%02X size=%u", message->type, frameSize);
    
    // Send the complete CRSF frame to the backpack UART
    BackpackOrLogStrm->write((const uint8_t *)message, frameSize);
}

#endif // PLATFORM_ESP32 && OPT_USE_TX_BACKPACK
