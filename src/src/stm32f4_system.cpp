// Board-level overrides for the STM32F411 targets. Both replace WEAK definitions in the stock
// genericSTM32F411CE variant, the STM32duino way of customising a generic variant.
#if defined(PLATFORM_STM32) && defined(STM32F4xx)

#include <Arduino.h>

// The PLL settings below assume the framework's default 8 MHz crystal.
#if HSE_VALUE != 8000000U
#error "SystemClock_Config() expects an 8 MHz HSE"
#endif

// SYSCLK 96 MHz, USB 48 MHz, APB1 48 MHz, APB2 96 MHz. The stock generic variant runs from HSI,
// which is not accurate enough for USB.
extern "C" void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {};
    RCC_ClkInitTypeDef clk = {};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 4;
    osc.PLL.PLLN = 96;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        // No crystal: same frequencies from HSI rather than Error_Handler(), so the LED and button
        // still work for diagnosis. USB is out of spec on HSI. LSE and the RTC clock stay off
        // (PC14 is a GPIO on these boards).
        osc = {};
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        osc.PLL.PLLState = RCC_PLL_ON;
        osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
        osc.PLL.PLLM = 16;
        osc.PLL.PLLN = 192;
        osc.PLL.PLLP = RCC_PLLP_DIV2;
        osc.PLL.PLLQ = 4;
        if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        {
            Error_Handler();
        }
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3) != HAL_OK)
    {
        Error_Handler();
    }
}

// Data lines only. HAL_PCD_MspInit() configures every pin in this map, and the stock one also has
// PA8 (SOF), PA9 (VBUS) and PA10 (ID), which are radio and GPIO pins on these boards. VBUS
// sensing is off (usbd_conf.c), so none of the three is needed. Declared extern in PeripheralPins.h
// without C linkage; variable names are not mangled, so this still replaces the C definition.
const PinMap PinMap_USB_OTG_FS[] = {
    {PA_11, USB_OTG_FS, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_AF10_OTG_FS)}, // USB_OTG_FS_DM
    {PA_12, USB_OTG_FS, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_AF10_OTG_FS)}, // USB_OTG_FS_DP
    {NC,    NP,         0}
};

#endif
