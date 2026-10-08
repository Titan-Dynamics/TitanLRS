#include "targets.h"

#if defined(PLATFORM_STM32) && defined(HAS_W25Q64_CONFIG)

#include "hardware_override_stm32.h"
#include "elrs_eeprom.h"
#include "options.h"
#include "W25Q64.h"

static_assert(RESERVED_EEPROM_SIZE + 10 <= HW_OVERRIDE_FLASH_ADDR,
              "the elrs_eeprom blob in sector 0 runs into the hardware override sector");
static_assert(sizeof(hw_override_header_t) + ELRSOPTS_HARDWARE_SIZE <= 4096,
              "the hardware override does not fit in its 4 KB sector");

static constexpr uint32_t W25Q64_PAGE_SIZE = 256;

bool hwOverride_Load(String &out)
{
    W25Q64 *flash = elrs_ConfigFlash();
    if (flash == nullptr)
    {
        return false;
    }

    hw_override_header_t header;
    flash->read(HW_OVERRIDE_FLASH_ADDR, (uint8_t *)&header, sizeof(header));
    const uint16_t len = header.len;
    if (len == 0 || len > ELRSOPTS_HARDWARE_SIZE)
    {
        return false;
    }

    char *json = (char *)malloc(len + 1);
    if (json == nullptr)
    {
        return false;
    }
    flash->read(HW_OVERRIDE_FLASH_ADDR + sizeof(header), (uint8_t *)json, len);
    json[len] = '\0';

    const bool valid = hwOverride_IsValid(header, (const uint8_t *)json, fw_options_discriminator());
    if (valid)
    {
        out = json;
    }
    free(json);
    return valid;
}

bool hwOverride_Save(const char *json, const size_t len)
{
    W25Q64 *flash = elrs_ConfigFlash();
    if (flash == nullptr || len == 0 || len > ELRSOPTS_HARDWARE_SIZE)
    {
        return false;
    }

    hw_override_header_t header;
    header.magic[0] = 'T';
    header.magic[1] = 'L';
    header.magic[2] = 'H';
    header.magic[3] = 'W';
    header.version = HW_OVERRIDE_VERSION;
    header.len = (uint16_t)len;
    header.discriminator = fw_options_discriminator();
    header.crc32 = elrs_crc32_update(0, (const uint8_t *)json, len);

    flash->sectorErase4K(HW_OVERRIDE_FLASH_ADDR);

    // Header then payload as one stream, programmed in pieces that never cross a page boundary.
    uint32_t addr = HW_OVERRIDE_FLASH_ADDR;
    const uint8_t *src = (const uint8_t *)&header;
    size_t remaining = sizeof(header);
    for (int part = 0; part < 2; ++part)
    {
        while (remaining > 0)
        {
            const uint32_t pageLeft = W25Q64_PAGE_SIZE - (addr % W25Q64_PAGE_SIZE);
            const uint32_t chunk = remaining < pageLeft ? remaining : pageLeft;
            flash->pageProgram(addr, src, chunk);
            addr += chunk;
            src += chunk;
            remaining -= chunk;
        }
        src = (const uint8_t *)json;
        remaining = len;
    }
    return true;
}

void hwOverride_Clear()
{
    W25Q64 *flash = elrs_ConfigFlash();
    if (flash != nullptr)
    {
        flash->sectorErase4K(HW_OVERRIDE_FLASH_ADDR);
    }
}

#endif
