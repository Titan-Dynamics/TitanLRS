// Unified STM32H743 RX — every board-specific value comes from the hardware layout the web
// flasher patches into the firmware slot (lib/OPTIONS/options.h titanSlot), as on the ESP unified
// targets. These are the only STM32 builds, so this header defines every pin and feature macro
// the code uses (include/targets.h supplies no defaults). The radio type comes from the env, the
// crystal and clock tree from the board.

#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

#define TITAN_UNIFIED_STM32

// Radio
#define GPIO_PIN_NSS hardware_pin(HARDWARE_radio_nss)
#define GPIO_PIN_MOSI hardware_pin(HARDWARE_radio_mosi)
#define GPIO_PIN_MISO hardware_pin(HARDWARE_radio_miso)
#define GPIO_PIN_SCK hardware_pin(HARDWARE_radio_sck)
#define GPIO_PIN_RST hardware_pin(HARDWARE_radio_rst)
#define GPIO_PIN_DIO1 hardware_pin(HARDWARE_radio_dio1)
#define GPIO_PIN_BUSY hardware_pin(HARDWARE_radio_busy)

// Second radio (Gemini); absent from the layout on single-radio boards
#define GPIO_PIN_NSS_2 hardware_pin(HARDWARE_radio_nss_2)
#define GPIO_PIN_RST_2 hardware_pin(HARDWARE_radio_rst_2)
#define GPIO_PIN_DIO1_2 hardware_pin(HARDWARE_radio_dio1_2)
#define GPIO_PIN_BUSY_2 hardware_pin(HARDWARE_radio_busy_2)

// Antenna switch (diversity / Gemini path select); absent from the layout when the board has none
#define GPIO_PIN_ANT_CTRL hardware_pin(HARDWARE_ant_ctrl)
#define GPIO_PIN_ANT_CTRL_COMPL hardware_pin(HARDWARE_ant_ctrl_compl)

// No I2C peripherals (screen, g-sensor, thermal sensor)
#define GPIO_PIN_SCL UNDEF_PIN
#define GPIO_PIN_SDA UNDEF_PIN

// SPI NOR flash holding the persisted config (elrs_eeprom) and the hardware override
#define W25Q64_CS_PIN hardware_pin(HARDWARE_config_flash_cs)
#define W25Q64_SCK_PIN hardware_pin(HARDWARE_config_flash_sck)
#define W25Q64_MISO_PIN hardware_pin(HARDWARE_config_flash_miso)
#define W25Q64_MOSI_PIN hardware_pin(HARDWARE_config_flash_mosi)

// Serial
#define GPIO_PIN_RCSIGNAL_RX hardware_pin(HARDWARE_serial_rx)
#define GPIO_PIN_RCSIGNAL_TX hardware_pin(HARDWARE_serial_tx)

// Debug logging via USB CDC (Serial)
#ifdef DEBUG_LOG
#define GPIO_PIN_DEBUG_RX    UNDEF_PIN
#define GPIO_PIN_DEBUG_TX    UNDEF_PIN
#define DEBUG_LOG_PORT       Serial
#endif

#ifdef USBCON
#define USBD_VID             0x1209
#define USBD_PID             0x0001
#define USB_MANUFACTURER     "Titan Dynamics"
#endif

// LEDs
#define GPIO_PIN_LED_RED (hardware_pin(HARDWARE_led_red) == UNDEF_PIN ? hardware_pin(HARDWARE_led) : hardware_pin(HARDWARE_led_red))
#define GPIO_LED_RED_INVERTED hardware_flag(HARDWARE_led_red_invert)

#define GPIO_PIN_LED_GREEN hardware_pin(HARDWARE_led_green)
#define GPIO_PIN_LED_BLUE hardware_pin(HARDWARE_led_blue)
#define GPIO_LED_BLUE_INVERTED hardware_flag(HARDWARE_led_blue_invert)
#define GPIO_LED_GREEN_INVERTED hardware_flag(HARDWARE_led_green_invert)
#define OPT_WS2812_IS_GRB               0
#define GPIO_PIN_LED_WS2812             UNDEF_PIN
#define WS2812_STATUS_LEDS_COUNT        0
#define WS2812_VTX_STATUS_LEDS_COUNT    0
#define WS2812_BOOT_LEDS_COUNT          0

// Buttons
#define GPIO_PIN_BUTTON hardware_pin(HARDWARE_button)
#define GPIO_BUTTON_ACTIVE_HIGH hardware_flag(HARDWARE_button_active_high)
#define GPIO_PIN_BUTTON2 hardware_pin(HARDWARE_button2)
#define GPIO_BUTTON2_ACTIVE_HIGH hardware_flag(HARDWARE_button2_active_high)

#define OPT_USE_HARDWARE_DCDC hardware_flag(HARDWARE_radio_dcdc)

// Power output
#define MinPower (PowerLevels_e)hardware_int(HARDWARE_power_min)
#define MaxPower (PowerLevels_e)hardware_int(HARDWARE_power_max)
#define DefaultPower (PowerLevels_e)hardware_int(HARDWARE_power_default)
#define POWER_OUTPUT_VALUES hardware_i16_array(HARDWARE_power_values)
#define POWER_OUTPUT_VALUES_COUNT hardware_int(HARDWARE_power_values_count)
#define POWER_OUTPUT_VALUES2 hardware_i16_array(HARDWARE_power_values2)
#define POWER_OUTPUT_VALUES2_COUNT POWER_OUTPUT_VALUES_COUNT
#define POWER_OUTPUT_VALUES_DUAL hardware_i16_array(HARDWARE_power_values_dual)
#define POWER_OUTPUT_VALUES_DUAL_COUNT hardware_int(HARDWARE_power_values_dual_count)
#define POWER_OUTPUT_DACWRITE           0

// No PWM servo outputs
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

// Cooling fan, run whenever the receiver is on: on/off (misc_fan_en) or PWM on a timer pin
// (misc_fan_pwm), driven by lib/THERMAL. The tacho input is ESP32-only.
#define GPIO_PIN_FAN_EN hardware_pin(HARDWARE_misc_fan_en)
#define GPIO_PIN_FAN_PWM hardware_pin(HARDWARE_misc_fan_pwm)
#define GPIO_PIN_FAN_TACHO UNDEF_PIN
#define GPIO_PIN_FAN_SPEEDS hardware_u16_array(HARDWARE_misc_fan_speeds)
#define GPIO_PIN_FAN_SPEEDS_COUNT hardware_int(HARDWARE_misc_fan_speeds_count)

// Not supported on the STM32 targets: thermal and g-sensors, SPI VTX.
#define OPT_HAS_THERMAL                 false
#define OPT_HAS_THERMAL_LM75A           false
#define OPT_HAS_GSENSOR                 false
#define OPT_HAS_VTX_SPI                 false
#define GPIO_PIN_SPI_VTX_NSS            UNDEF_PIN
