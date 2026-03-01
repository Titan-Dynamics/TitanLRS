#ifndef DEVICE_NAME
#define DEVICE_NAME "TD Freedom Edition 900RX"
#endif

#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

#define TARGET_DIY_900_RX_WEACT_H743

// GPIO pin definitions
// SPI for SX127x radio (RFM95 on HDZero HALO FC)
// Using SPI1 hardware bus shared with gyro sensors
#define GPIO_PIN_NSS         PE0   // UART8_RX pad - Chip Select
#define GPIO_PIN_MOSI        PA7   // SPI1_SDO (SPI1_MOSI)
#define GPIO_PIN_MISO        PA6   // SPI1_SDI (SPI1_MISO)
#define GPIO_PIN_SCK         PA5   // SPI1_SCK

// On-board W25Q64 SPI NOR flash shares SPI1 - hold its CS high
#define FLASH_CS_PIN         PD6   // W25Q64 SPI NOR flash chip select (active-low)

#define GPIO_PIN_RST         PE7   // UART7_RX pad - Radio Reset
#define GPIO_PIN_DIO0        PE8   // UART7_TX pad - RX Done IRQ
#define GPIO_PIN_DIO1        PE1   // UART8_TX pad - FHSS Change IRQ
// #define GPIO_PIN_BUSY     // Not used on SX127x

// No second radio
#define GPIO_PIN_NSS_2       UNDEF_PIN
#define GPIO_PIN_DIO0_2      UNDEF_PIN
#define GPIO_PIN_RST_2       UNDEF_PIN

// UART for CRSF communication
#ifdef DEBUG_LOG
// When debug logging is enabled, Serial is USB CDC, so use USART1 for CRSF
#define TARGET_DIY_900_RX_WEACT_H743_DEBUG
#define GPIO_PIN_RCSIGNAL_RX PB7   // USART1_RX
#define GPIO_PIN_RCSIGNAL_TX PB6   // USART1_TX
#else
#define GPIO_PIN_RCSIGNAL_RX PB7   // USART1_RX
#define GPIO_PIN_RCSIGNAL_TX PB6   // USART1_TX
#endif

// Debug logging via USB CDC (Serial)
#ifdef DEBUG_LOG
#define GPIO_PIN_DEBUG_RX    UNDEF_PIN
#define GPIO_PIN_DEBUG_TX    UNDEF_PIN
// Use USB CDC for debug output
#define DEBUG_LOG_PORT       Serial
// STM32 USB CDC configuration
#define USBD_VID             0x0483
#define USBD_PID             0x5740
#define USB_MANUFACTURER     "Titan Dynamics"
#define USB_PRODUCT          "Freedom Edition 900RX"
#endif

// LED (active-high on WeAct Mini H743)
#define GPIO_PIN_LED_RED     PE3
#define GPIO_LED_RED_INVERTED 0
#define GPIO_PIN_LED_BLUE       UNDEF_PIN
#define GPIO_LED_BLUE_INVERTED  0
#define GPIO_LED_GREEN_INVERTED 0
#define OPT_WS2812_IS_GRB      0
#define GPIO_PIN_LED_WS2812     UNDEF_PIN
#define WS2812_STATUS_LEDS_COUNT 0
#define WS2812_VTX_STATUS_LEDS_COUNT 0
#define WS2812_BOOT_LEDS_COUNT  0
#ifndef __ASSEMBLER__
#define WS2812_STATUS_LEDS      ((const int16_t *)nullptr)
#define WS2812_VTX_STATUS_LEDS  ((const int16_t *)nullptr)
#define WS2812_BOOT_LEDS        ((const int16_t *)nullptr)
#endif

// Button (boot button on WeAct Mini)
#define GPIO_PIN_BUTTON      PC13
#define GPIO_PIN_BUTTON2     UNDEF_PIN

// Use flash-emulated EEPROM (no external I2C EEPROM)
#define STM32_USE_FLASH

#ifndef RADIO_SX127X
#define RADIO_SX127X
#endif

// Output Power - SX127x PA_BOOST lookup table
// Values are OCP register settings for 10mW, 25mW, 50mW
#define MinPower                    PWR_10mW
#define MaxPower                    PWR_50mW
#define DefaultPower                PWR_10mW
#define POWER_OUTPUT_VALUES_COUNT   3
#ifndef __ASSEMBLER__
static const int16_t _power_values[] = {120, 124, 127};
#define POWER_OUTPUT_VALUES         _power_values
#define POWER_OUTPUT_VALUES2        _power_values
#endif
#define POWER_OUTPUT_VALUES2_COUNT  POWER_OUTPUT_VALUES_COUNT
#define POWER_OUTPUT_DACWRITE       0
#define OPT_USE_SX1276_RFO_HF      0

// No PWM servo outputs on this target
#define GPIO_PIN_PWM_OUTPUTS_COUNT  0
#ifndef __ASSEMBLER__
#define GPIO_PIN_PWM_OUTPUTS        ((const int16_t *)nullptr)
#endif
#define OPT_HAS_SERVO_OUTPUT        0

// No analog VBAT
#define GPIO_ANALOG_VBAT            UNDEF_PIN
#define ANALOG_VBAT_OFFSET          0
#define ANALOG_VBAT_SCALE           1
