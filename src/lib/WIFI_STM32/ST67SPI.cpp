#include "ST67SPI.h"

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void ST67SPI::begin(uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin,
                    uint8_t csPin, uint8_t rdyPin, uint32_t spiSpeed)
{
    if (_spi != nullptr) {
        _spi->end();
        delete _spi;
        _spi = nullptr;
    }

    _csPin    = csPin;
    _rdyPin   = rdyPin;
    _spiSpeed = spiSpeed;

    // CS is active HIGH on this module — start LOW (deasserted)
    pinMode(_csPin, OUTPUT);
    csLow();

    // RDY is driven by the module
    pinMode(_rdyPin, INPUT);

    // STM32duino auto-selects the SPI peripheral from the pin numbers.
    // Constructor order: MOSI, MISO, SCLK
    _spi = new SPIClass(mosiPin, misoPin, sckPin);
    _spi->begin();
}

bool ST67SPI::waitReady(uint32_t timeoutMs)
{
    uint32_t t0 = millis();
    while (!digitalRead(_rdyPin)) {
        if (millis() - t0 > timeoutMs) {
#if SPI_DEBUG_HEX
            Serial.printf("[SPI] waitReady timeout (%lu ms) RDY=%d\n",
                          (unsigned long)timeoutMs, (int)digitalRead(_rdyPin));
#endif
            return false;
        }
        yield();
    }
    return true;
}

bool ST67SPI::waitNotReady(uint32_t timeoutMs)
{
    uint32_t t0 = millis();
    while (digitalRead(_rdyPin)) {
        if (millis() - t0 > timeoutMs) {
#if SPI_DEBUG_HEX
            Serial.printf("[SPI] waitNotReady timeout (%lu ms) RDY=%d\n",
                          (unsigned long)timeoutMs, (int)digitalRead(_rdyPin));
#endif
            return false;
        }
        yield();
    }
    return true;
}

bool ST67SPI::isReady()
{
    return digitalRead(_rdyPin) == HIGH;
}

// ---------------------------------------------------------------------------
// sendFrame — build 8-byte header, then clock out header + padded payload
// ---------------------------------------------------------------------------
int ST67SPI::sendFrame(const uint8_t* data, uint16_t len)
{
    uint16_t pLen = padLen(len);

    // Build 8-byte header
    uint8_t hdr[SPI_HEADER_SIZE];
    hdr[0] = SPI_SYNC_BYTE0;           // 0xAA
    hdr[1] = SPI_SYNC_BYTE1;           // 0x55
    hdr[2] = (uint8_t)(len & 0xFF);    // data length low
    hdr[3] = (uint8_t)(len >> 8);      // data length high
    hdr[4] = 0x00;                     // frame (version=0, no flags)
    hdr[5] = SPI_FRAME_TYPE_AT;        // type = AT commands
    hdr[6] = 0x00;                     // reserved
    hdr[7] = 0x00;                     // reserved

    // Assert CS, wait for module to be ready
    csHigh();
    if (!waitReady(SPI_RDY_TIMEOUT_MS)) {
        csLow();
        return -1;  // timeout
    }

    delayMicroseconds(10);  // settling time after CS assert

    _spi->beginTransaction(SPISettings(_spiSpeed, MSBFIRST, SPI_MODE0));

    // Clock out header
    for (uint8_t i = 0; i < SPI_HEADER_SIZE; i++) {
        _spi->transfer(hdr[i]);
    }

    // Clock out payload (pad short frames with 0x88)
    for (uint16_t i = 0; i < pLen; i++) {
        _spi->transfer(i < len ? data[i] : SPI_PAD_BYTE);
    }

    _spi->endTransaction();

    // Protocol: CS must stay HIGH until RDY goes LOW
    if (!waitNotReady(SPI_RDY_TIMEOUT_MS)) {
#if SPI_DEBUG_HEX
        Serial.println(F("[SPI] WARN: RDY stayed HIGH after sendFrame"));
#endif
    }
    csLow();

#if SPI_DEBUG_HEX
    hexDump("TX-H", hdr, SPI_HEADER_SIZE);
    hexDump("TX-D", data, len);
#endif

    return (int)len;
}

// ---------------------------------------------------------------------------
// recvFrame — read header to learn payload length, then read payload
// ---------------------------------------------------------------------------
int ST67SPI::recvFrame(uint8_t* buf, uint16_t maxLen)
{
    csHigh();

    if (!waitReady(SPI_RDY_TIMEOUT_MS)) {
        csLow();
        return -1;  // timeout
    }

    delayMicroseconds(10);  // settling time after CS assert

    _spi->beginTransaction(SPISettings(_spiSpeed, MSBFIRST, SPI_MODE0));

    // Read 8-byte header (send 0x00 dummy bytes)
    uint8_t hdr[SPI_HEADER_SIZE];
    for (uint8_t i = 0; i < SPI_HEADER_SIZE; i++) {
        hdr[i] = _spi->transfer(0x00);
    }

    // Validate sync word (strict on-wire order: AA 55)
    if (hdr[0] != SPI_SYNC_BYTE0 || hdr[1] != SPI_SYNC_BYTE1) {
        _spi->endTransaction();
        if (!waitNotReady(SPI_RDY_TIMEOUT_MS)) {
#if SPI_DEBUG_HEX
            Serial.println(F("[SPI] WARN: RDY stayed HIGH after bad header"));
#endif
        }
        csLow();
#if SPI_DEBUG_HEX
        hexDump("RX-BAD-HDR", hdr, SPI_HEADER_SIZE);
#endif
        return -2;  // bad sync
    }

    uint16_t dataLen = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
    uint16_t pLen    = padLen(dataLen);
    uint16_t copyLen = (dataLen < maxLen) ? dataLen : maxLen;

    // Read payload bytes
    for (uint16_t i = 0; i < pLen; i++) {
        uint8_t b = _spi->transfer(0x00);
        if (i < copyLen) {
            buf[i] = b;
        }
    }

    _spi->endTransaction();

    if (!waitNotReady(SPI_RDY_TIMEOUT_MS)) {
#if SPI_DEBUG_HEX
        Serial.println(F("[SPI] WARN: RDY stayed HIGH after recvFrame"));
#endif
    }
    csLow();

#if SPI_DEBUG_HEX
    hexDump("RX-H", hdr, SPI_HEADER_SIZE);
    hexDump("RX-D", buf, copyLen);
#endif

    return (int)copyLen;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ST67SPI::csHigh() { digitalWrite(_csPin, HIGH); }
void ST67SPI::csLow()  { digitalWrite(_csPin, LOW);  }

uint16_t ST67SPI::padLen(uint16_t len)
{
    return (len + 3U) & ~3U;   // round up to next multiple of 4
}

#if SPI_DEBUG_HEX
void ST67SPI::hexDump(const char* dir, const uint8_t* buf, uint16_t len)
{
    Serial.printf("[SPI %s %u] ", dir, len);
    uint16_t show = (len > 64) ? 64 : len;
    for (uint16_t i = 0; i < show; i++) {
        Serial.printf("%02X ", buf[i]);
    }
    if (len > show) Serial.print("...");
    Serial.println();
}
#endif
