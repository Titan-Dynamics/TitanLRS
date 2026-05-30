// W25Q64 is STM32-only by design (used as the on-board SPI NOR config store
// on our STM32H7 targets via HAS_W25Q64_CONFIG).
#include "targets.h"
#if defined(PLATFORM_STM32)

#include "W25Q64.h"
#include "logging.h"
#include <Arduino.h>

// W25Q64 commands we use
#define W25_CMD_JEDEC_ID      0x9F
#define W25_CMD_READ_STATUS_1 0x05
#define W25_CMD_WRITE_ENABLE  0x06
#define W25_CMD_SECTOR_ERASE  0x20  // 4 KB
#define W25_CMD_PAGE_PROGRAM  0x02
#define W25_CMD_READ_DATA     0x03

#define W25_STATUS_BUSY 0x01

// Conservative bus clock — well within the LR1121 module wiring and the
// W25Q64JV's 50 MHz cap for the 0x03 read command.
#define W25Q64_SPI_HZ 10000000UL

W25Q64::W25Q64(uint32_t cs_pin, uint32_t sck_pin, uint32_t miso_pin, uint32_t mosi_pin)
    : _cs(cs_pin), _sck(sck_pin), _miso(miso_pin), _mosi(mosi_pin),
#if defined(PLATFORM_STM32)
      _spi((uint32_t)mosi_pin, (uint32_t)miso_pin, (uint32_t)sck_pin),
#else
      _spi(),
#endif
      _settings(W25Q64_SPI_HZ, MSBFIRST, SPI_MODE0)
{
}

bool W25Q64::begin()
{
    // Take CS high FIRST, before any clocking, to keep the chip deselected
    // through SWO traffic on PB3 (JTDO-TRACESWO) and during bus init.
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);

#if defined(PLATFORM_STM32)
    _spi.setMOSI(_mosi);
    _spi.setMISO(_miso);
    _spi.setSCLK(_sck);
#endif
    _spi.begin();

    uint8_t id[3] = {0};
    readJedecId(id);
    DBGLN("W25Q64 JEDEC ID: %u %u %u", (unsigned)id[0], (unsigned)id[1], (unsigned)id[2]);

    // Accept any responding SPI-NOR
    return id[0] != 0x00 && id[0] != 0xFF;
}

void W25Q64::selectCs()
{
    _spi.beginTransaction(_settings);
    digitalWrite(_cs, LOW);
}

void W25Q64::deselectCs()
{
    digitalWrite(_cs, HIGH);
    _spi.endTransaction();
}

void W25Q64::readJedecId(uint8_t out[3])
{
    selectCs();
    _spi.transfer(W25_CMD_JEDEC_ID);
    out[0] = _spi.transfer(0x00);
    out[1] = _spi.transfer(0x00);
    out[2] = _spi.transfer(0x00);
    deselectCs();
}

void W25Q64::writeEnable()
{
    selectCs();
    _spi.transfer(W25_CMD_WRITE_ENABLE);
    deselectCs();
}

void W25Q64::waitWhileBusy()
{
    // Spin reading status register until WIP clears. Worst case is sector
    // erase at ~400 ms typ. Caller is responsible for not calling this
    // from a context where that latency is fatal.
    uint8_t s;
    do
    {
        selectCs();
        _spi.transfer(W25_CMD_READ_STATUS_1);
        s = _spi.transfer(0x00);
        deselectCs();
    } while (s & W25_STATUS_BUSY);
}

void W25Q64::read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    selectCs();
    _spi.transfer(W25_CMD_READ_DATA);
    _spi.transfer((addr >> 16) & 0xFF);
    _spi.transfer((addr >> 8) & 0xFF);
    _spi.transfer(addr & 0xFF);
    for (uint32_t i = 0; i < len; i++)
    {
        buf[i] = _spi.transfer(0x00);
    }
    deselectCs();
}

void W25Q64::sectorErase4K(uint32_t addr)
{
    writeEnable();
    selectCs();
    _spi.transfer(W25_CMD_SECTOR_ERASE);
    _spi.transfer((addr >> 16) & 0xFF);
    _spi.transfer((addr >> 8) & 0xFF);
    _spi.transfer(addr & 0xFF);
    deselectCs();
    waitWhileBusy();
}

void W25Q64::pageProgram(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    writeEnable();
    selectCs();
    _spi.transfer(W25_CMD_PAGE_PROGRAM);
    _spi.transfer((addr >> 16) & 0xFF);
    _spi.transfer((addr >> 8) & 0xFF);
    _spi.transfer(addr & 0xFF);
    for (uint32_t i = 0; i < len; i++)
    {
        _spi.transfer(buf[i]);
    }
    deselectCs();
    waitWhileBusy();
}

#endif // PLATFORM_STM32
