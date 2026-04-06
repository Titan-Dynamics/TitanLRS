#ifndef ST67SPI_H
#define ST67SPI_H

#include <Arduino.h>
#include <SPI.h>

// SPI frame constants (from the ST67W611M1 SPI interface specification)
#define SPI_SYNC_BYTE0      0xAA    // First byte of sync word on wire
#define SPI_SYNC_BYTE1      0x55    // Second byte of sync word on wire
#define SPI_PAD_BYTE        0x88    // Host-side padding value
#define SPI_HEADER_SIZE     8       // 2 sync + 2 length + 1 frame + 1 type + 2 reserved

// Frame byte fields
#define SPI_FRAME_TYPE_AT   0x00    // AT command traffic
#define SPI_FRAME_TYPE_STA  0x01    // Station data
#define SPI_FRAME_TYPE_AP   0x02    // Access-Point data

// Default timeouts
#define SPI_RDY_TIMEOUT_MS      5000
#define SPI_BOOT_TIMEOUT_MS     10000

// Enable to hex-dump SPI transactions on Serial (heavy; disables upload throughput).
#ifndef SPI_DEBUG_HEX
#define SPI_DEBUG_HEX   0
#endif

class ST67SPI {
public:
    /// Initialise SPI peripheral + control GPIOs.
    void begin(uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin,
               uint8_t csPin, uint8_t rdyPin, uint32_t spiSpeed);

    /// Block until SPI_RDY goes HIGH or timeout.  Returns true on success.
    bool waitReady(uint32_t timeoutMs = SPI_RDY_TIMEOUT_MS);

    /// Block until SPI_RDY goes LOW or timeout.  Returns true on success.
    bool waitNotReady(uint32_t timeoutMs = SPI_RDY_TIMEOUT_MS);

    /// Non-blocking check of SPI_RDY pin.
    bool isReady();

    /// Send a framed SPI message (8-byte header + padded payload).
    /// Returns bytes sent on success, negative on error.
    int sendFrame(const uint8_t* data, uint16_t len);

    /// Receive a framed SPI message (reads header, then payload).
    /// Returns bytes of payload placed in buf, negative on error.
    int recvFrame(uint8_t* buf, uint16_t maxLen);

private:
    SPIClass* _spi = nullptr;
    uint8_t  _csPin = 0;
    uint8_t  _rdyPin = 0;
    uint32_t _spiSpeed = 0;

    void     csHigh();
    void     csLow();
    uint16_t padLen(uint16_t len);      // round up to next multiple of 4

#if SPI_DEBUG_HEX
    void     hexDump(const char* dir, const uint8_t* buf, uint16_t len);
#endif
};

#endif // ST67SPI_H
