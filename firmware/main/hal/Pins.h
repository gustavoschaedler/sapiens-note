#pragma once
// GPIO pin map for the Waveshare ESP32-S3 1.54" e-Paper (touch).
//
// TODO(board): fill these from the Waveshare schematic / demo `pin_config.h`.
// The product page does not publish a GPIO table; see ../../docs/HARDWARE.md.
// Values below are PLACEHOLDERS — do not flash until confirmed.

#include "driver/gpio.h"

namespace sapiens::fw::pins {

// Shared I2C bus (FT6336 touch, ES8311 control, PCF85063 RTC, SHTC3 sensor)
constexpr gpio_num_t I2C_SDA = GPIO_NUM_NC; // TODO(board)
constexpr gpio_num_t I2C_SCL = GPIO_NUM_NC; // TODO(board)

// e-Paper (SPI)
constexpr gpio_num_t EPD_MOSI = GPIO_NUM_NC; // TODO(board)
constexpr gpio_num_t EPD_SCK  = GPIO_NUM_NC; // TODO(board)
constexpr gpio_num_t EPD_CS   = GPIO_NUM_NC; // TODO(board)
constexpr gpio_num_t EPD_DC   = GPIO_NUM_NC; // TODO(board)
constexpr gpio_num_t EPD_RST  = GPIO_NUM_NC; // TODO(board)
constexpr gpio_num_t EPD_BUSY = GPIO_NUM_NC; // TODO(board)

// Touch FT6336 interrupt
constexpr gpio_num_t TOUCH_INT = GPIO_NUM_NC; // TODO(board)

// Audio ES8311 (I2S)
constexpr gpio_num_t I2S_MCLK = GPIO_NUM_NC; // TODO(board)
constexpr gpio_num_t I2S_BCLK = GPIO_NUM_NC; // TODO(board)
constexpr gpio_num_t I2S_LRCK = GPIO_NUM_NC; // TODO(board)
constexpr gpio_num_t I2S_DOUT = GPIO_NUM_NC; // TODO(board)
constexpr gpio_num_t I2S_DIN  = GPIO_NUM_NC; // TODO(board)

// microSD (SPI or SDMMC) — confirm mode
constexpr gpio_num_t SD_CS = GPIO_NUM_NC; // TODO(board)

// Buttons / battery
constexpr gpio_num_t BTN_PWR = GPIO_NUM_NC;  // TODO(board)
constexpr gpio_num_t BTN_BOOT = GPIO_NUM_0;  // BOOT is GPIO0 on ESP32-S3
// Battery voltage divider on an ADC-capable pin
constexpr gpio_num_t BAT_ADC = GPIO_NUM_NC;  // TODO(board)

// I2C device addresses
constexpr int FT6336_ADDR = 0x38;
constexpr int ES8311_ADDR = 0x18;
constexpr int PCF85063_ADDR = 0x51;
constexpr int SHTC3_ADDR = 0x70;

} // namespace sapiens::fw::pins
