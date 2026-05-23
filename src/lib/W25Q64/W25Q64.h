#pragma once

#include <stdint.h>
#include <SPI.h>

class W25Q64
{
public:
    W25Q64(uint32_t cs_pin, uint32_t sck_pin, uint32_t miso_pin, uint32_t mosi_pin);

    // Drives CS high (before any SPI traffic), inits the SPI bus, reads JEDEC ID.
    // Returns true if the JEDEC manufacturer byte looks like Winbond (0xEF).
    bool begin();

    void readJedecId(uint8_t out[3]);

    // Read len bytes starting at flash byte address addr into buf.
    void read(uint32_t addr, uint8_t *buf, uint32_t len);

    // Erase one 4 KB sector (addr is the byte address of any byte in the sector).
    // Blocks until the chip reports not-busy.
    void sectorErase4K(uint32_t addr);

    // Page-program up to 256 bytes within a single 256-byte page.
    // Blocks until the chip reports not-busy.
    void pageProgram(uint32_t addr, const uint8_t *buf, uint32_t len);

private:
    void selectCs();
    void deselectCs();
    void writeEnable();
    void waitWhileBusy();

    uint32_t _cs;
    uint32_t _sck;
    uint32_t _miso;
    uint32_t _mosi;
    SPIClass _spi;
    SPISettings _settings;
};
