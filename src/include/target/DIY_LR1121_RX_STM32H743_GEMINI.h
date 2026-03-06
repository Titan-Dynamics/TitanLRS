#ifndef DEVICE_NAME
#define DEVICE_NAME "TD-DB STM32H7 G"
#endif

#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

#define TARGET_DIY_LR1121_RX_STM32H743_GEMINI

// GPIO pin definitions
#define GPIO_PIN_NSS         PE0   // Chip Select
#define GPIO_PIN_MOSI        PA7   // SPI1_SDO (SPI1_MOSI)
#define GPIO_PIN_MISO        PA6   // SPI1_SDI (SPI1_MISO)
#define GPIO_PIN_SCK         PA5   // SPI1_SCK
#define GPIO_PIN_RST         PE7   // Radio 1 Reset
#define GPIO_PIN_DIO1        PE1   // LR1121 DIO9 IRQ (Radio 1)
#define GPIO_PIN_BUSY        PE9   // BUSY (Radio 1)

// Second LR1121 on shared SPI1
#define GPIO_PIN_NSS_2       PE8   // Chip Select (Radio 2)
#define GPIO_PIN_RST_2       PE10  // Radio 2 Reset
#define GPIO_PIN_DIO1_2      PE11  // LR1121 DIO9 IRQ (Radio 2)
#define GPIO_PIN_BUSY_2      PE12  // BUSY (Radio 2)

// On-board W25Q64 SPI NOR flash shares SPI1 - hold its CS high
#define FLASH_CS_PIN         PD6

// CRSF UART
#define GPIO_PIN_RCSIGNAL_RX PB7   // USART1_RX
#define GPIO_PIN_RCSIGNAL_TX PB6   // USART1_TX

// Debug logging via USB CDC (Serial)
#ifdef DEBUG_LOG
#define GPIO_PIN_DEBUG_RX    UNDEF_PIN
#define GPIO_PIN_DEBUG_TX    UNDEF_PIN
#define DEBUG_LOG_PORT       Serial
#define USBD_VID             0x0483
#define USBD_PID             0x5740
#define USB_MANUFACTURER     "Titan Dynamics"
#define USB_PRODUCT          "TD-DB STM32H7 Gemini"
#endif

// LEDs
#define GPIO_PIN_LED_RED                PE3
#define GPIO_LED_RED_INVERTED           0

#define GPIO_PIN_LED_BLUE               UNDEF_PIN
#define GPIO_LED_BLUE_INVERTED          0
#define GPIO_LED_GREEN_INVERTED         0
#define OPT_WS2812_IS_GRB               0
#define GPIO_PIN_LED_WS2812             UNDEF_PIN
#define WS2812_STATUS_LEDS_COUNT        0
#define WS2812_VTX_STATUS_LEDS_COUNT    0
#define WS2812_BOOT_LEDS_COUNT          0

// Button (boot button on WeAct Mini)
#define GPIO_PIN_BUTTON                 PC13
#define GPIO_PIN_BUTTON2                UNDEF_PIN

// Use flash-emulated EEPROM (no external I2C EEPROM)
#define STM32_USE_FLASH

#ifndef RADIO_LR1121
#define RADIO_LR1121
#endif

// LR1121 configuration
#define OPT_USE_HARDWARE_DCDC      true
#define OPT_USE_SX1276_RFO_HF      0

// RF switch control — default config for standard LR1121 modules
// [RfswEnable, StbyCfg, RxCfg, TxCfg, TxHPCfg, TxHfCfg, Unused, WifiCfg]
#ifndef __ASSEMBLER__
static const uint16_t _rfsw_ctrl[] = {0x0F, 0x00, 0x04, 0x08, 0x08, 0x02, 0x00, 0x01};
#define LR1121_RFSW_CTRL             _rfsw_ctrl
#endif
#define LR1121_RFSW_CTRL_COUNT       8

// Power output — LR1121 dBm values
// Sub-GHz power levels: 12, 16, 19, 22 dBm
// 2.4GHz power levels: -10, -6, -3, 1 dBm
#define MinPower                        PWR_10mW
#define MaxPower                        PWR_100mW
#define DefaultPower                    PWR_10mW
#define POWER_OUTPUT_VALUES_COUNT       4
#ifndef __ASSEMBLER__
static const int16_t _power_values[] = {12, 16, 19, 22};
#define POWER_OUTPUT_VALUES             _power_values
#define POWER_OUTPUT_VALUES2            _power_values
#define POWER_OUTPUT_VALUES2_COUNT      POWER_OUTPUT_VALUES_COUNT
static const int16_t _power_values_dual[] = {-10, -6, -3, 1};
#define POWER_OUTPUT_VALUES_DUAL        _power_values_dual
#endif
#define POWER_OUTPUT_VALUES_DUAL_COUNT  4
#define POWER_OUTPUT_DACWRITE           0

// No PWM servo outputs on this target
#define GPIO_PIN_PWM_OUTPUTS_COUNT      0
#define OPT_HAS_SERVO_OUTPUT            0

// No analog VBAT
#define GPIO_ANALOG_VBAT                UNDEF_PIN
#define ANALOG_VBAT_OFFSET              0
#define ANALOG_VBAT_SCALE               1

#ifndef __ASSEMBLER__
#define WS2812_STATUS_LEDS      ((const int16_t *)nullptr)
#define WS2812_VTX_STATUS_LEDS  ((const int16_t *)nullptr)
#define WS2812_BOOT_LEDS        ((const int16_t *)nullptr)

#define GPIO_PIN_PWM_OUTPUTS    ((const int16_t *)nullptr)
#endif
