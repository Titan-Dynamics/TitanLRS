#ifndef DEVICE_NAME
#define DEVICE_NAME "WeAct H743 900TX"
#endif

#define TARGET_DIY_900_TX_WEACT_H743

#define BACKPACK_LOGGING_BAUD 420000

// GPIO pin definitions
// SPI for SX127x radio
#define GPIO_PIN_NSS         PA4
#define GPIO_PIN_MOSI        PA7
#define GPIO_PIN_MISO        PA6
#define GPIO_PIN_SCK         PA5

#define GPIO_PIN_RST         PB0
#define GPIO_PIN_DIO0        PB10
#define GPIO_PIN_DIO1        PB11
#define GPIO_PIN_BUSY        PC7

// RF switch control (typical for TX modules)
#define GPIO_PIN_RX_ENABLE   PC8
#define GPIO_PIN_TX_ENABLE   PC9

// UART for CRSF communication
#define GPIO_PIN_RCSIGNAL_RX PA10  // UART1
#define GPIO_PIN_RCSIGNAL_TX PA9   // UART1

// Debug UART
#define GPIO_PIN_DEBUG_RX    PA3   // UART2
#define GPIO_PIN_DEBUG_TX    PA2   // UART2

// LED
#define GPIO_PIN_LED_RED     PE3
#define GPIO_LED_RED_INVERTED 0

// Button (boot button on WeAct Mini)
#define GPIO_PIN_BUTTON      PC13

// Buzzer (optional)
#define GPIO_PIN_BUZZER      PD14

// Fan control (optional)
#define GPIO_PIN_FAN_EN      PD15

// Use flash-emulated EEPROM (no external I2C EEPROM)
#define STM32_USE_FLASH

#ifndef RADIO_SX127X
#define RADIO_SX127X
#endif

// Output Power
#define MinPower                    PWR_10mW
#define MaxPower                    PWR_50mW
#define POWER_OUTPUT_VALUES         {120,124,127}
