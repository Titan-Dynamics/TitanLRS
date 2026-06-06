#ifndef DEVICE_NAME
#define DEVICE_NAME "TD LR2021 STM32H7 TX"
#endif

#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

#define TARGET_DIY_LR2021_TX_STM32H743

// Radio on SPI4 (same pin map as the LR1121 STM32H7 TX target)
#define GPIO_PIN_NSS         PE0   // Chip Select
#define GPIO_PIN_MOSI        PE14  // SPI4_MOSI
#define GPIO_PIN_MISO        PE13  // SPI4_MISO
#define GPIO_PIN_SCK         PE12  // SPI4_SCK
#define GPIO_PIN_RST         PE7   // Radio Reset
#define GPIO_PIN_DIO1        PE1   // LR2021 DIO9 IRQ (module DIO9 -> this MCU pin)
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

// CRSF UART — half-duplex single-wire on PB10 (USART3_TX)
#define GPIO_PIN_RCSIGNAL_RX PB10
#define GPIO_PIN_RCSIGNAL_TX PB10

// USB CDC descriptors (always-on for TX — Serial-over-USB / MAVLink forward)
#ifdef USBCON
#define USBD_VID             0x0483
#define USBD_PID             0x5740
#define USB_MANUFACTURER     "Titan Dynamics"
#define USB_PRODUCT          "TD LR2021 STM32H7 TX"
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

// BOOT0 on the STM32H7 is a strapping-only pin and can't be read as
// a GPIO at runtime, so a custom board should route the (active-high) BOOT0 button net
// to spare GPIO PE2 for the runtime user-button role
#define GPIO_PIN_BUTTON                 PE2
#define GPIO_BUTTON_ACTIVE_HIGH         1
#define GPIO_PIN_BUTTON2                UNDEF_PIN

// LR2021 configuration
#ifndef RADIO_LR2021
#define RADIO_LR2021
#endif

#define OPT_USE_HARDWARE_DCDC      true

// Power output — LR2021 SetTxParams register values (NiceRF LoRa2021 datasheet §8).
// These are register values written directly to SetTxParams (NOT dBm); the chip
// integrated PA is selected per band by SetPaConfig. Bench-tunable (see plan §1.5).
//   Sub-GHz (LF PA) regs  19/25/31/37 ~= +10/+13/+17/+20 dBm
//   2.4 GHz  (HF PA) regs   0/ 8/16/24 ~=  +1/ +5/ +8/+12 dBm
#define MinPower                        PWR_10mW
#define MaxPower                        PWR_100mW
#define DefaultPower                    PWR_10mW
#define POWER_OUTPUT_VALUES_COUNT       4
#ifndef __ASSEMBLER__
static const int16_t _power_values[] = {19, 25, 31, 37};
#define POWER_OUTPUT_VALUES             _power_values
#define POWER_OUTPUT_VALUES2            _power_values
#define POWER_OUTPUT_VALUES2_COUNT      POWER_OUTPUT_VALUES_COUNT
static const int16_t _power_values_dual[] = {0, 8, 16, 24};
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
