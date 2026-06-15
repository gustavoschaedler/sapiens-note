#pragma once
// GPIO pin map for the Sapiens Note board (ESP32-S3, 1.54" 200x200 e-paper).
//
// Confirmed against the working Pala Note reference firmware
// (027_Pala_Note_firmware / config.h) — the boards share the same layout.
// These are REAL pins, not placeholders.

#include "driver/gpio.h"

namespace sapiens::fw::pins {

// Shared I2C bus (FT6336 touch, ES8311 control, PCF85063 RTC, SHTC3 sensor)
constexpr gpio_num_t I2C_SDA = GPIO_NUM_47;
constexpr gpio_num_t I2C_SCL = GPIO_NUM_48;

// e-Paper (SPI2_HOST). CS is driven manually by the driver (not by the SPI bus).
constexpr int        EPD_SPI_HOST = 2; // SPI2_HOST
constexpr gpio_num_t EPD_MOSI = GPIO_NUM_13;
constexpr gpio_num_t EPD_SCK  = GPIO_NUM_12;
constexpr gpio_num_t EPD_CS   = GPIO_NUM_11;
constexpr gpio_num_t EPD_DC   = GPIO_NUM_10;
constexpr gpio_num_t EPD_RST  = GPIO_NUM_9;
constexpr gpio_num_t EPD_BUSY = GPIO_NUM_8;

// Power rails (board_power_bsp): EPD + audio are active-LOW, VBAT latch active-HIGH.
constexpr gpio_num_t EPD_PWR   = GPIO_NUM_6;  // enable EPD rail (ON = low)
constexpr gpio_num_t AUDIO_PWR = GPIO_NUM_42; // enable audio rail (ON = low)
constexpr gpio_num_t VBAT_PWR  = GPIO_NUM_17; // battery power latch (ON = high)

// Touch FT6336 interrupt (shares the I2C bus above)
constexpr gpio_num_t TOUCH_INT = GPIO_NUM_NC; // not wired on this board

// Audio ES8311 (I2S) — TODO(board): confirm I2S pins from the audio BSP
constexpr gpio_num_t I2S_MCLK = GPIO_NUM_NC;
constexpr gpio_num_t I2S_BCLK = GPIO_NUM_NC;
constexpr gpio_num_t I2S_LRCK = GPIO_NUM_NC;
constexpr gpio_num_t I2S_DOUT = GPIO_NUM_NC;
constexpr gpio_num_t I2S_DIN  = GPIO_NUM_NC;

// microSD (SDMMC 1-bit)
constexpr gpio_num_t SD_CLK = GPIO_NUM_39;
constexpr gpio_num_t SD_CMD = GPIO_NUM_41;
constexpr gpio_num_t SD_D0  = GPIO_NUM_40;

// Buttons / battery
constexpr gpio_num_t BTN_PWR = GPIO_NUM_18; // power button
constexpr gpio_num_t BTN_BOOT = GPIO_NUM_0; // BOOT is GPIO0 on ESP32-S3
// Battery voltage divider on ADC1_CH3
constexpr gpio_num_t BAT_ADC = GPIO_NUM_4;

// Status LEDs (NOT in the reference docs — set these once the real GPIOs are known;
// GPIO_NUM_NC keeps the indicator a no-op so nothing else is driven by mistake).
constexpr gpio_num_t LED_GREEN = GPIO_NUM_NC; // blinks while on USB power
constexpr gpio_num_t LED_RED   = GPIO_NUM_NC; // blinks when battery <= 10%
constexpr bool LED_ACTIVE_HIGH = true;        // flip if the LEDs are active-low

// I2C device addresses
constexpr int FT6336_ADDR = 0x38;
constexpr int ES8311_ADDR = 0x18;
constexpr int PCF85063_ADDR = 0x51;
constexpr int SHTC3_ADDR = 0x70;

} // namespace sapiens::fw::pins
