#include "stm32_dfu.h"

#if defined(PLATFORM_STM32)

#include <Arduino.h>

// ---- per-family constants -------------------------------------------------------------
// Adding a family means adding a block here; the flag,
// request and jump code below stay as they are.
#if defined(STM32H7xx)
// STM32H742/43/50/53 system memory (ROM bootloader) base
#define STM32_SYSMEM_BOOT_ADDR   0x1FF09800UL
// Cortex-M7: the caches must be off before handing over to the ROM
#define STM32_DFU_DISABLE_CACHES 1
#elif defined(STM32F4xx)
// STM32F4 system memory (ROM bootloader) base, AN2606
#define STM32_SYSMEM_BOOT_ADDR   0x1FFF0000UL
// Map system memory at 0x00000000 before the jump, as the ROM expects
#define STM32_DFU_REMAP_SYSMEM   1
// The radios keep their DIO configuration across an MCU reset, and radio 1's IRQ line is on PA9,
// which the F4 ROM bootloader probes as USART1_TX. Reset them before handing over.
#define STM32_DFU_RESET_RADIOS   1
#endif

// Flag value in RTC backup register 0 requesting ROM-DFU on next boot
#define DFU_BKP_MAGIC   0xDF11C0DEUL

static void backupDomainAccess()
{
#if defined(__HAL_RCC_PWR_CLK_ENABLE)
    __HAL_RCC_PWR_CLK_ENABLE();
#endif
#if defined(__HAL_RCC_RTC_CLK_ENABLE)
    __HAL_RCC_RTC_CLK_ENABLE();   // RTC APB register-interface clock
#endif
    HAL_PWR_EnableBkUpAccess();
}

static void jumpToSystemBootloader()
{
#if defined(__HAL_RCC_USB_OTG_FS_FORCE_RESET)
    // USB is already enumerated by premain(); resetting the OTG core detaches us cleanly, so the
    // host sees the ROM bootloader attach as a new device.
    __HAL_RCC_USB_OTG_FS_FORCE_RESET();
    __HAL_RCC_USB_OTG_FS_RELEASE_RESET();
#endif

    // Tear the system back down to near-reset state before handing over —
    // the ROM bootloader does its own clock and USB setup.
    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    HAL_RCC_DeInit();

#if defined(STM32_DFU_REMAP_SYSMEM)
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
#endif

    for (uint32_t i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

#if defined(STM32_DFU_DISABLE_CACHES)
    SCB_DisableICache();
    SCB_DisableDCache();
#endif

    __enable_irq();

    __set_MSP(*(volatile uint32_t *)STM32_SYSMEM_BOOT_ADDR);
    ((void (*)(void))(*(volatile uint32_t *)(STM32_SYSMEM_BOOT_ADDR + 4U)))();

    while (true) {}
}

void stm32_DfuCheckAndJump()
{
    backupDomainAccess();
    if (RTC->BKP0R == DFU_BKP_MAGIC)
    {
        RTC->BKP0R = 0;
        jumpToSystemBootloader();
    }
}

// Called from main() before setup() (overrides the weak core definition). On this core USB is
// already up by now (premain), hence the teardown in jumpToSystemBootloader().
void initVariant()
{
    stm32_DfuCheckAndJump();
}

#if defined(STM32_DFU_RESET_RADIOS)
static void resetRadios()
{
    const int rst[] = {GPIO_PIN_RST, GPIO_PIN_RST_2};
    for (const int pin : rst)
    {
        if (pin != UNDEF_PIN)
        {
            pinMode(pin, OUTPUT);
            digitalWrite(pin, LOW);
        }
    }
    delay(1);
    for (const int pin : rst)
    {
        if (pin != UNDEF_PIN)
        {
            digitalWrite(pin, HIGH);
        }
    }
}
#endif

void stm32_RequestDfu()
{
    backupDomainAccess();
    RTC->BKP0R = DFU_BKP_MAGIC;
    delay(50);   // let any in-flight USB TX drain
#if defined(STM32_DFU_RESET_RADIOS)
    resetRadios();
#endif
    NVIC_SystemReset();
}

#endif // PLATFORM_STM32
