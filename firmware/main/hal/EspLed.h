#pragma once
// Status-LED indicator: blinks the green LED while on USB power and the red LED
// when the battery is low. Driven by a small task. Pins come from Pins.h
// (LED_GREEN/LED_RED); GPIO_NUM_NC channels are simply skipped.
namespace sapiens::fw {

class EspLed {
public:
    void init();                 // configure pins + start the blink task
    void set_battery(int pct);   // feed the latest battery % (-1 = unknown)
};

} // namespace sapiens::fw
