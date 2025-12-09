#pragma once

#include "targets.h"

#if defined(PLATFORM_ESP32) && defined(OPT_USE_TX_BACKPACK)

#include "CRSFConnector.h"
#include "CRSFParser.h"

/**
 * @class BackpackCRSF
 * @brief Handles CRSF protocol communication over the TX Backpack UART connection.
 *
 * This class implements a CRSFConnector to enable CRSF parameter protocol support
 * over the backpack UART (BackpackOrLogStrm). It works alongside MSP and MAVLink
 * protocols that also use the same UART.
 *
 * The backpack UART connection allows the TX Backpack web interface to:
 * - Discover CRSF devices (DEVICE_PING/DEVICE_INFO)
 * - Read parameters from CRSF devices (PARAM_READ/PARAM_ENTRY)
 * - Write parameters to CRSF devices (PARAM_WRITE)
 *
 * This connector processes CRSF frames received from the backpack and forwards
 * responses from the main firmware's CRSF endpoints (e.g., ELRS_LUA device).
 */
class BackpackCRSF : public CRSFConnector {
public:
    BackpackCRSF();

    /**
     * @brief Initialize the backpack CRSF connector.
     *
     * Registers this connector with the CRSF router and sets up the backpack
     * as a device that can be reached via this connection.
     */
    void begin();

    /**
     * @brief Process a single byte from the backpack UART.
     *
     * Call this for each byte received from BackpackOrLogStrm to parse
     * incoming CRSF frames. Works alongside MSP and MAVLink parsers
     * that also examine each byte.
     *
     * @param byte The byte received from the backpack UART
     */
    void processReceivedByte(uint8_t byte);

    /**
     * @brief Forward a CRSF message to the backpack UART.
     *
     * Sends CRSF frames (DEVICE_INFO, PARAM_ENTRY, etc.) to the TX Backpack
     * over the BackpackOrLogStrm serial port.
     *
     * @param message Pointer to the CRSF message to send
     */
    void forwardMessage(const crsf_header_t *message) override;

private:
    CRSFParser parser;
};

// Global instance
extern BackpackCRSF backpackCRSF;

#endif // PLATFORM_ESP32 && OPT_USE_TX_BACKPACK
