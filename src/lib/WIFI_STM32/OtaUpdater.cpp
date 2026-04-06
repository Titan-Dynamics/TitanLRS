#include "OtaUpdater.h"

#include <cstring>
#include <stm32h7xx_hal.h>
#include "crc32_util.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr uint32_t INACTIVE_BANK_ADDR   = FLASH_BANK2_BASE;
static constexpr uint32_t SECTORS_PER_BANK     = FLASH_SECTOR_TOTAL;
static constexpr uint32_t SECTOR_SIZE_BYTES    = FLASH_SECTOR_SIZE;
static constexpr uint32_t FLASH_WAIT_TIMEOUT_MS = 60000u;    // 60 s / sector
static constexpr uint32_t FLASH_QW_SPIN_MAX    = 0x20000000u;
static constexpr uint32_t FLASH_QW_MASK        = 0x00000004u;
static constexpr uint32_t FLASH_ERR_MASK       = 0x07EE0000u;
static constexpr uint32_t FLASH_CLEAR_MASK     = 0x0FEF0000u;

static uint32_t flashBankSizeBytes() {
    return FLASH_SIZE / 2u;
}

static bool waitForQWClear(volatile uint32_t* SRx, uint32_t timeoutMs,
                           uint32_t* waitedMsOut, uint32_t* spinsOut)
{
    const uint32_t t0 = millis();
    uint32_t spins = 0;
    while ((*SRx & FLASH_QW_MASK) != 0u) {
        spins++;
        if ((uint32_t)(millis() - t0) > timeoutMs || spins >= FLASH_QW_SPIN_MAX) {
            if (waitedMsOut) *waitedMsOut = (uint32_t)(millis() - t0);
            if (spinsOut)    *spinsOut    = spins;
            return false;
        }
    }
    if (waitedMsOut) *waitedMsOut = (uint32_t)(millis() - t0);
    if (spinsOut)    *spinsOut    = spins;
    return true;
}

static HAL_StatusTypeDef eraseSectorManual(volatile uint32_t* CRx,
                                           volatile uint32_t* SRx,
                                           volatile uint32_t* CCRx,
                                           uint32_t sector,
                                           uint32_t* elapsedMsOut,
                                           uint32_t* spinsOut)
{
    const uint32_t t0 = millis();
    uint32_t waitedMs = 0;
    uint32_t spins = 0;

    // Clear stale status flags before starting this sector.
    *CCRx = FLASH_CLEAR_MASK;

    // Ensure controller is idle before arming SER.
    if (!waitForQWClear(SRx, FLASH_WAIT_TIMEOUT_MS, &waitedMs, &spins)) {
        if (elapsedMsOut) *elapsedMsOut = (uint32_t)(millis() - t0);
        if (spinsOut)    *spinsOut = spins;
        return HAL_TIMEOUT;
    }

    // Program sector erase for the selected bank.
    *CRx &= ~(FLASH_CR_SER | FLASH_CR_BER | FLASH_CR_SNB | FLASH_CR_PSIZE);
    *CRx |= (FLASH_CR_SER
          |  FLASH_VOLTAGE_RANGE_4
          |  (sector << FLASH_CR_SNB_Pos)
          |  FLASH_CR_START);

    if (!waitForQWClear(SRx, FLASH_WAIT_TIMEOUT_MS, &waitedMs, &spins)) {
        *CRx &= ~(FLASH_CR_SER | FLASH_CR_SNB);
        if (elapsedMsOut) *elapsedMsOut = (uint32_t)(millis() - t0);
        if (spinsOut)    *spinsOut = spins;
        return HAL_TIMEOUT;
    }

    // Clear erase bits and status bits.
    *CRx &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    *CCRx = FLASH_CLEAR_MASK;

    if ((*SRx & FLASH_ERR_MASK) != 0u) {
        if (elapsedMsOut) *elapsedMsOut = (uint32_t)(millis() - t0);
        if (spinsOut)    *spinsOut = spins;
        return HAL_ERROR;
    }

    if (elapsedMsOut) *elapsedMsOut = (uint32_t)(millis() - t0);
    if (spinsOut)    *spinsOut = spins;
    return HAL_OK;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void OtaUpdater::setError(const char* text) {
    strncpy(_lastError, text, sizeof(_lastError) - 1);
    _lastError[sizeof(_lastError) - 1] = '\0';
}

// ---------------------------------------------------------------------------
// begin — unlock flash, erase required sectors in inactive bank, init CRC
// ---------------------------------------------------------------------------
bool OtaUpdater::begin(uint32_t imageSize) {
    if (_started) {
        // Allow restart — abort previous attempt so erase can be retried.
        abort();
    }
    const uint32_t maxImageSize = flashBankSizeBytes();
    if (imageSize == 0 || imageSize > maxImageSize) {
        setError("invalid image size");
        return false;
    }

    _imageSize      = imageSize;
    _bytesReceived  = 0;
    _writeAddress   = INACTIVE_BANK_ADDR;
    _crc            = crc32_init();
    _wordBufLen     = 0;
    memset(_wordBuf, 0xFF, sizeof(_wordBuf));
    _lastError[0]   = '\0';

    if (HAL_FLASH_Unlock() != HAL_OK) {
        setError("flash unlock failed");
        return false;
    }
    _flashUnlocked = true;

    if (!eraseInactiveBank()) {
        abort();
        return false;
    }

    _started = true;
    return true;
}

// ---------------------------------------------------------------------------
// eraseInactiveBank — erase one sector at a time, yield between sectors
// ---------------------------------------------------------------------------
bool OtaUpdater::eraseInactiveBank() {
    // OTA image is always written to FLASH_BANK2_BASE (0x08100000) address
    // space. With SWAP_BANK enabled, memory/register mapping is swapped by HW,
    // so we still target the Bank2 register set for this mapped region.
    const uint32_t targetMappedBank = FLASH_BANK_2;

    volatile uint32_t* CRx  = &FLASH->CR2;
    volatile uint32_t* SRx  = &FLASH->SR2;
    volatile uint32_t* CCRx = &FLASH->CCR2;

    // Store for use by programFlashWord() — avoids HAL bank-selection bug.
    _bankCR  = CRx;
    _bankSR  = SRx;
    _bankCCR = CCRx;

    Serial.printf("[OTA] Erasing mapped bank %lu @0x%08lX\n",
                  (unsigned long)targetMappedBank,
                  (unsigned long)INACTIVE_BANK_ADDR);

    uint32_t sectorsToErase = (_imageSize + SECTOR_SIZE_BYTES - 1u) / SECTOR_SIZE_BYTES;
    if (sectorsToErase == 0u || sectorsToErase > SECTORS_PER_BANK) {
        setError("invalid erase sector count");
        return false;
    }
    Serial.printf("[OTA] Erasing %lu/%lu sector(s)\n",
                  (unsigned long)sectorsToErase,
                  (unsigned long)SECTORS_PER_BANK);

    for (uint32_t sector = 0; sector < sectorsToErase; sector++) {
        Serial.printf("[OTA] Erasing sector %lu...\n", (unsigned long)sector);

        uint32_t elapsedMs = 0;
        uint32_t spins = 0;
        HAL_StatusTypeDef rc = eraseSectorManual(CRx, SRx, CCRx, sector, &elapsedMs, &spins);
        if (rc != HAL_OK) {
            char buf[120];
            snprintf(buf, sizeof(buf),
                     "erase fail s%lu b%lu HAL%d t=%lums spins=%lu SR=0x%08lX CR=0x%08lX",
                     (unsigned long)sector,
                     (unsigned long)targetMappedBank,
                     (int)rc,
                     (unsigned long)elapsedMs,
                     (unsigned long)spins,
                     (unsigned long)*SRx,
                     (unsigned long)*CRx);
            setError(buf);
            return false;
        }

        Serial.printf("[OTA] Sector %lu erased (%lu ms, spins=%lu)\n",
                      (unsigned long)sector,
                      (unsigned long)elapsedMs,
                      (unsigned long)spins);
        yield();  // let Wi-Fi service between sectors
    }

    return true;
}

// ---------------------------------------------------------------------------
// programFlashWord — write one 32-byte flash word with interrupts disabled
//
// We bypass HAL_FLASH_Program so erase/program always use the same selected
// bank register set as eraseInactiveBank() (mapped Bank2 address space).
// ---------------------------------------------------------------------------
bool OtaUpdater::programFlashWord(uint32_t address, const uint8_t* data32) {
    if ((address & 31u) != 0u) {
        setError("unaligned flash address");
        return false;
    }
    if (_bankCR == nullptr || _bankSR == nullptr || _bankCCR == nullptr) {
        setError("bank registers not initialized");
        return false;
    }

    volatile uint32_t* CR  = _bankCR;
    volatile uint32_t* SR  = _bankSR;
    volatile uint32_t* CCR = _bankCCR;

    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    // Clear stale error flags
    *CCR = 0x0FEF0000u;

    // Wait for flash not busy (QW bit = bit 2)
    for (uint32_t t = 0; t < 0x01000000u; t++) {
        if ((*SR & 0x4u) == 0) goto ready;
    }
    if ((primask & 1u) == 0u) {
        __enable_irq();
    }
    setError("flash busy before program");
    return false;
ready:

    // Set PG (bit 1) + PSIZE = 11 (32-byte / x64, bits 4-5)
    *CR &= ~FLASH_CR_PSIZE;            // clear PSIZE
    *CR |= FLASH_CR_PG                  // PG    (bit 1 = 0x02)
          | FLASH_VOLTAGE_RANGE_4;     // PSIZE (bits 4-5 = 0x30)

    // Write the 32-byte flash word as eight 32-bit words.
    // The H7 flash controller latches writes into an internal buffer and
    // triggers the actual program operation after the 8th word.
    volatile uint32_t* dest = (volatile uint32_t*)(uintptr_t)address;
    const uint32_t* src = (const uint32_t*)(const void*)data32;
    __ISB();
    __DSB();
    for (int i = 0; i < 8; i++) {
        dest[i] = src[i];
    }
    __DSB();

    // Wait for programming to complete (QW flag)
    for (uint32_t t = 0; t < 0x01000000u; t++) {
        if ((*SR & 0x4u) == 0) goto done;
    }
    *CR &= ~FLASH_CR_PG;               // clear PG
    if ((primask & 1u) == 0u) {
        __enable_irq();
    }
    setError("flash program timeout");
    return false;
done:

    // Clear PG bit
    *CR &= ~FLASH_CR_PG;

    if ((primask & 1u) == 0u) {
        __enable_irq();
    }

    // Check for error flags (bits 17-26 of SR)
    if (*SR & 0x07EE0000u) {
        char buf[80];
        snprintf(buf, sizeof(buf), "flash prog err SR=0x%08lX @0x%08lX",
                 (unsigned long)*SR, (unsigned long)address);
        setError(buf);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// writeChunk — buffer into 32-byte flash words, accumulate CRC
// ---------------------------------------------------------------------------
bool OtaUpdater::writeChunk(const uint8_t* data, size_t len) {
    if (!_started) {
        setError("update not started");
        return false;
    }
    if (data == nullptr || len == 0) {
        return true;
    }
    if (_bytesReceived + (uint32_t)len > _imageSize) {
        setError("payload larger than content-length");
        return false;
    }

    // Accumulate CRC over the raw incoming data
    _crc = crc32_update(_crc, data, len);

    size_t offset = 0;
    while (offset < len) {
        const size_t remaining = len - offset;
        const size_t space     = 32u - _wordBufLen;
        const size_t toCopy    = (remaining < space) ? remaining : space;

        memcpy(_wordBuf + _wordBufLen, data + offset, toCopy);
        _wordBufLen += (uint8_t)toCopy;
        offset += toCopy;

        if (_wordBufLen == 32u) {
            if (!programFlashWord(_writeAddress, _wordBuf)) {
                return false;
            }
            _writeAddress += 32u;
            _wordBufLen = 0;
            memset(_wordBuf, 0xFF, sizeof(_wordBuf));
        }
    }

    _bytesReceived += (uint32_t)len;
    return true;
}

// ---------------------------------------------------------------------------
// flushWriteBuffer — pad partial word with 0xFF and program it
// ---------------------------------------------------------------------------
bool OtaUpdater::flushWriteBuffer() {
    if (_wordBufLen == 0) {
        return true;
    }

    // Pad remainder with 0xFF (erased state)
    memset(_wordBuf + _wordBufLen, 0xFF, 32u - _wordBufLen);

    if (!programFlashWord(_writeAddress, _wordBuf)) {
        return false;
    }

    _writeAddress += 32u;
    _wordBufLen = 0;
    memset(_wordBuf, 0xFF, sizeof(_wordBuf));
    return true;
}

// ---------------------------------------------------------------------------
// finalize — flush, CRC verify, toggle SWAP_BANK, system reset
// ---------------------------------------------------------------------------
bool OtaUpdater::finalize() {
    if (!_started) {
        setError("update not started");
        return false;
    }
    if (_bytesReceived != _imageSize) {
        setError("incomplete payload");
        return false;
    }

    // Flush any partial flash word
    if (!flushWriteBuffer()) {
        abort();
        return false;
    }

    // Finalize the write CRC
    const uint32_t writeCrc = crc32_finalize(_crc);

    // Invalidate D-cache before read-back verification
    SCB_CleanInvalidateDCache();

    // Read back from flash and compute verification CRC
    uint32_t verifyCrc = crc32_init();
    verifyCrc = crc32_update(verifyCrc, (const uint8_t*)INACTIVE_BANK_ADDR, _imageSize);
    verifyCrc = crc32_finalize(verifyCrc);

    if (verifyCrc != writeCrc) {
        char buf[80];
        snprintf(buf, sizeof(buf), "CRC mismatch: wrote 0x%08lX read 0x%08lX",
                 (unsigned long)writeCrc, (unsigned long)verifyCrc);
        setError(buf);
        abort();
        return false;
    }

    Serial.printf("[OTA] CRC verified: 0x%08lX (%lu bytes)\n",
                  (unsigned long)writeCrc, (unsigned long)_imageSize);

    // --- Toggle SWAP_BANK option bit ---
    if (HAL_FLASH_OB_Unlock() != HAL_OK) {
        setError("option byte unlock failed");
        abort();
        return false;
    }

    FLASH_OBProgramInitTypeDef ob;
    HAL_FLASHEx_OBGetConfig(&ob);

    ob.OptionType = OPTIONBYTE_USER;
    ob.USERType   = OB_USER_SWAP_BANK;
    ob.USERConfig = (ob.USERConfig & OB_SWAP_BANK_ENABLE)
                  ? OB_SWAP_BANK_DISABLE
                  : OB_SWAP_BANK_ENABLE;

    if (HAL_FLASHEx_OBProgram(&ob) != HAL_OK) {
        setError("option byte program failed");
        HAL_FLASH_OB_Lock();
        abort();
        return false;
    }

    Serial.println(F("[OTA] SWAP_BANK toggled — launching system reset..."));
    Serial.flush();
    delay(100);  // let CDC serial drain before reset

    // HAL_FLASH_OB_Launch() should trigger an immediate system reset.
    // On some stacks it can return; in that case caller schedules fallback reset.
    if (HAL_FLASH_OB_Launch() != HAL_OK) {
        setError("option byte launch failed");
        HAL_FLASH_OB_Lock();
        abort();
        return false;
    }

    // Returned unexpectedly: treat as success and let caller reset via fallback path.
    HAL_FLASH_OB_Lock();
    if (_flashUnlocked) {
        HAL_FLASH_Lock();
        _flashUnlocked = false;
    }
    _started = false;
    return true;
}

// ---------------------------------------------------------------------------
// abort — lock flash, reset state
// ---------------------------------------------------------------------------
void OtaUpdater::abort() {
    if (_flashUnlocked) {
        HAL_FLASH_Lock();
        _flashUnlocked = false;
    }
    _bankCR = nullptr;
    _bankSR = nullptr;
    _bankCCR = nullptr;
    _imageSize = 0;
    _bytesReceived = 0;
    _writeAddress = 0;
    _crc = 0;
    _wordBufLen = 0;
    memset(_wordBuf, 0xFF, sizeof(_wordBuf));
    _started = false;
}
