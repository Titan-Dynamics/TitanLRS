#ifndef OTA_UPDATER_H
#define OTA_UPDATER_H

#include <Arduino.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// OtaUpdater — bank-swap OTA writer for STM32H743
//
// Writes firmware to the inactive flash bank (0x08100000), verifies CRC32,
// then toggles the SWAP_BANK option bit and triggers a system reset.
// The CPU always boots from 0x08000000 — after swap the new firmware is
// mapped there by hardware.
// ---------------------------------------------------------------------------
class OtaUpdater {
public:
    /// Erase the inactive bank and prepare for streaming.
    /// @param imageSize  Total firmware size in bytes (from Content-Length).
    bool begin(uint32_t imageSize);

    /// Write a chunk of firmware data.  Buffers internally to 32-byte
    /// flash-word boundaries (H7 requirement).
    bool writeChunk(const uint8_t* data, size_t len);

    /// Flush any partial flash word, verify CRC32 read-back, toggle
    /// SWAP_BANK, and trigger system reset via HAL_FLASH_OB_Launch().
    /// On success this function does NOT return (system resets).
    /// Returns false only on error.
    bool finalize();

    /// Abort an in-progress update and lock flash.
    void abort();

    /// True after begin() succeeds, false after finalize()/abort().
    bool isStarted() const { return _started; }

    /// The image size passed to begin().
    uint32_t imageSize() const { return _imageSize; }

    /// Human-readable description of the last error.
    const char* lastError() const { return _lastError; }

private:
    bool eraseInactiveBank();
    bool programFlashWord(uint32_t address, const uint8_t* data32);
    bool flushWriteBuffer();
    void setError(const char* text);

    bool     _started        = false;
    bool     _flashUnlocked  = false;
    uint32_t _imageSize      = 0;
    uint32_t _bytesReceived  = 0;
    uint32_t _writeAddress   = 0;
    uint32_t _crc            = 0;

    // Physical bank register pointers (set during begin, used for programming).
    // These always point to the correct CRx/SRx/CCRx for the INACTIVE physical
    // bank, regardless of the SWAP_BANK state.
    volatile uint32_t* _bankCR  = nullptr;
    volatile uint32_t* _bankSR  = nullptr;
    volatile uint32_t* _bankCCR = nullptr;

    alignas(32) uint8_t _wordBuf[32];
    uint8_t  _wordBufLen     = 0;

    char     _lastError[96]  = {0};
};

#endif
