#ifndef DEVICE_NAME
#define DEVICE_NAME "TD-DB STM32H7"
#endif

#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

#define TARGET_DIY_LR1121_RX_STM32H743

// Radio on SPI4
#define GPIO_PIN_NSS         PE0   // Chip Select
#define GPIO_PIN_MOSI        PE14  // SPI4_MOSI
#define GPIO_PIN_MISO        PE13  // SPI4_MISO
#define GPIO_PIN_SCK         PE12  // SPI4_SCK
#define GPIO_PIN_RST         PE7   // Radio Reset
#define GPIO_PIN_DIO1        PE1   // LR1121 DIO9 IRQ
#define GPIO_PIN_BUSY        PE9   // BUSY

// LCD shares SPI4 on the WeAct. Mute it at boot:
//   PE11 = LCD_CS  -> hold HIGH (deassert, ignore bus)
//   PE10 = LCD_LED -> hold HIGH  (backlight off)
#define SUPPRESS_LCD            1
#define GPIO_PIN_LCD_CS         PE11
#define GPIO_PIN_LCD_BACKLIGHT  PE10

// On-board W25Q64 SPI NOR flash on SPI1 (PB3 SCK, PB4 MISO, PD7 MOSI, PD6 CS)
// Used as the EEPROM config on this target
#define HAS_W25Q64_CONFIG
#define W25Q64_SCK_PIN       PB3
#define W25Q64_MISO_PIN      PB4
#define W25Q64_MOSI_PIN      PD7
#define W25Q64_CS_PIN        PD6

// CRSF UART
#define GPIO_PIN_RCSIGNAL_RX PB11  // USART3_RX
#define GPIO_PIN_RCSIGNAL_TX PB10  // USART3_TX

// Debug logging via USB CDC (Serial)
#ifdef DEBUG_LOG
#define GPIO_PIN_DEBUG_RX    UNDEF_PIN
#define GPIO_PIN_DEBUG_TX    UNDEF_PIN
#define DEBUG_LOG_PORT       Serial
#define USBD_VID             0x0483
#define USBD_PID             0x5740
#define USB_MANUFACTURER     "Titan Dynamics"
#define USB_PRODUCT          "TD-DB STM32H7"
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

#ifndef RADIO_LR1121
#define RADIO_LR1121
#endif

// LR1121 configuration
#define OPT_USE_HARDWARE_DCDC      true
#define OPT_USE_SX1276_RFO_HF      0
// #define OPT_USE_LR1121_TCXO        1
//#define LR1121_TCXO_VOLTAGE        0x02  // RegTcxoTune: 0x02 = 1.8V

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
