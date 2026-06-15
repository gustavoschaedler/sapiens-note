#include "EspLed.h"

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "Pins.h"

namespace sapiens::fw {
namespace {
std::atomic<int> s_batt{-1};
constexpr int kLowBatt = 10; // %

void set_led(gpio_num_t pin, bool on) {
    if (pin == GPIO_NUM_NC) return;
    gpio_set_level(pin, (on == sapiens::fw::pins::LED_ACTIVE_HIGH) ? 1 : 0);
}

void led_task(void*) {
    namespace p = sapiens::fw::pins;
    bool phase = false;
    for (;;) {
        phase = !phase;                       // ~2 Hz blink (toggle every 250 ms)
        int batt = s_batt.load();
        bool low = (batt >= 0 && batt <= kLowBatt);
        bool usb = usb_serial_jtag_is_connected();
        // Low battery (red) takes priority; otherwise green while on USB.
        set_led(p::LED_RED, low && phase);
        set_led(p::LED_GREEN, (!low && usb) && phase);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
} // namespace

void EspLed::init() {
    namespace p = sapiens::fw::pins;
    std::uint64_t mask = 0;
    if (p::LED_GREEN != GPIO_NUM_NC) mask |= (1ULL << p::LED_GREEN);
    if (p::LED_RED != GPIO_NUM_NC) mask |= (1ULL << p::LED_RED);
    if (mask) {
        gpio_config_t cfg = {};
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pin_bit_mask = mask;
        gpio_config(&cfg);
        set_led(p::LED_GREEN, false);
        set_led(p::LED_RED, false);
    }
    xTaskCreate(led_task, "sn_led", 2048, nullptr, 4, nullptr);
}

void EspLed::set_battery(int pct) { s_batt = pct; }

} // namespace sapiens::fw
