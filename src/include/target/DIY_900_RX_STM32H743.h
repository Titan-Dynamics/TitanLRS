#ifndef DEVICE_NAME
#define DEVICE_NAME "TD900 STM32H7"
#endif

#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

#define TARGET_DIY_900_RX_STM32H743

// Radio (SX127x on SPI1)
#define GPIO_PIN_NSS         PE0   // Chip Select
#define GPIO_PIN_MOSI        PA7   // SPI1_SDO (SPI1_MOSI)
#define GPIO_PIN_MISO        PA6   // SPI1_SDI (SPI1_MISO)
#define GPIO_PIN_SCK         PA5   // SPI1_SCK
#define GPIO_PIN_RST         PE7   // Radio Reset
#define GPIO_PIN_DIO0        PE8   // RX Done IRQ
#define GPIO_PIN_DIO1        PE1   // FHSS Change IRQ

// On-board W25Q64 SPI NOR flash shares SPI1 - hold its CS high
#define FLASH_CS_PIN         PD6

// CRSF UART
#define GPIO_PIN_RCSIGNAL_RX PB7   // USART1_RX
#define GPIO_PIN_RCSIGNAL_TX PB6   // USART1_TX

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
#define USB_PRODUCT          "TD 900RX STM32H743"
#endif

// LEDs
#define GPIO_PIN_LED_RED        PE3
#define GPIO_LED_RED_INVERTED   0

#define GPIO_PIN_LED_BLUE       UNDEF_PIN
#define GPIO_LED_BLUE_INVERTED  0
#define GPIO_LED_GREEN_INVERTED 0
#define OPT_WS2812_IS_GRB      0
#define GPIO_PIN_LED_WS2812     UNDEF_PIN
#define WS2812_STATUS_LEDS_COUNT 0
#define WS2812_VTX_STATUS_LEDS_COUNT 0
#define WS2812_BOOT_LEDS_COUNT  0

// Button (boot button on WeAct Mini)
#define GPIO_PIN_BUTTON      PC13
#define GPIO_PIN_BUTTON2     UNDEF_PIN

// Use flash-emulated EEPROM (no external I2C EEPROM)
#define STM32_USE_FLASH

#ifndef RADIO_SX127X
#define RADIO_SX127X
#endif

// Output Power
// Values are 10mW, 25mW, 50mW
#define MinPower                    PWR_10mW
#define MaxPower                    PWR_50mW
#define DefaultPower                PWR_10mW
#define POWER_OUTPUT_VALUES_INIT    {120, 124, 127}
#define POWER_OUTPUT_VALUES_COUNT   3
#define POWER_OUTPUT_VALUES2_COUNT  POWER_OUTPUT_VALUES_COUNT
#define POWER_OUTPUT_DACWRITE       0
#define OPT_USE_SX1276_RFO_HF       0

// No PWM servo outputs on this target
#define GPIO_PIN_PWM_OUTPUTS_COUNT  0
#define OPT_HAS_SERVO_OUTPUT        0

// No analog VBAT
#define GPIO_ANALOG_VBAT            UNDEF_PIN
#define ANALOG_VBAT_OFFSET          0
#define ANALOG_VBAT_SCALE           1

#ifndef __ASSEMBLER__
#define WS2812_STATUS_LEDS      ((const int16_t *)nullptr)
#define WS2812_VTX_STATUS_LEDS  ((const int16_t *)nullptr)
#define WS2812_BOOT_LEDS        ((const int16_t *)nullptr)

static const int16_t _power_values[] = POWER_OUTPUT_VALUES_INIT;
#define POWER_OUTPUT_VALUES     _power_values
#define POWER_OUTPUT_VALUES2    _power_values

#define GPIO_PIN_PWM_OUTPUTS    ((const int16_t *)nullptr)
#endif
