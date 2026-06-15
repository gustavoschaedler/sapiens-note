#include "EspPower.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "Pins.h"

namespace sapiens::fw {
namespace {
const char* TAG = "sapiens.power";

// BAT_ADC = GPIO4 = ADC1 channel 3 on the ESP32-S3, behind a 2:1 divider.
constexpr adc_channel_t kBatChan = ADC_CHANNEL_3;
constexpr float kDivider = 2.0f;
constexpr int64_t kCacheUs = 10'000'000; // re-read at most every 10 s

adc_oneshot_unit_handle_t s_adc = nullptr;
adc_cali_handle_t s_cali = nullptr;
bool s_ready = false;
int s_cached = -1;
int64_t s_last_us = 0;

void ensure_adc() {
    if (s_ready) return;
    s_ready = true; // try once; leave handles null on failure
    adc_oneshot_unit_init_cfg_t ucfg = {};
    ucfg.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) { s_adc = nullptr; return; }
    adc_oneshot_chan_cfg_t ccfg = {};
    ccfg.atten = ADC_ATTEN_DB_12; // ~0..3.1V range at the pin
    ccfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(s_adc, kBatChan, &ccfg);

    adc_cali_curve_fitting_config_t cal = {};
    cal.unit_id = ADC_UNIT_1;
    cal.chan = kBatChan;
    cal.atten = ADC_ATTEN_DB_12;
    cal.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) s_cali = nullptr;
}

int pct_from_volts(float v) {
    // The ADC+divider reads a full Li-ion (~4.2V) as ~4.05-4.10V, so 100% is
    // anchored there instead of 4.20V (otherwise it caps around 90%).
    if (v >= 4.05f) return 100;
    if (v <= 3.30f) return 0;
    const float volts[] = {3.30f, 3.55f, 3.75f, 3.92f, 4.05f};
    const int pct[] = {0, 25, 50, 75, 100};
    for (int i = 1; i < 5; ++i) {
        if (v <= volts[i]) {
            float t = (v - volts[i - 1]) / (volts[i] - volts[i - 1]);
            int p = pct[i - 1] + (int)((pct[i] - pct[i - 1]) * t + 0.5f);
            p = ((p + 2) / 5) * 5; // round to 5%
            return p < 0 ? 0 : (p > 100 ? 100 : p);
        }
    }
    return 100;
}
} // namespace

int EspPower::battery_percent() const {
    int64_t now = esp_timer_get_time();
    if (s_cached >= 0 && (now - s_last_us) < kCacheUs) return s_cached;

    ensure_adc();
    if (!s_adc) return s_cached; // -1 until/if the ADC ever comes up

    long sum = 0;
    int ok = 0;
    for (int i = 0; i < 16; ++i) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, kBatChan, &raw) != ESP_OK) continue;
        int mv = raw;
        if (s_cali) adc_cali_raw_to_voltage(s_cali, raw, &mv);
        sum += mv;
        ++ok;
    }
    if (!ok) return s_cached;

    float v = (sum / (float)ok) / 1000.0f * kDivider;
    s_cached = (v <= 0.15f) ? -1 : pct_from_volts(v);
    s_last_us = now;
    ESP_LOGI(TAG, "battery %.2fV -> %d%%", v, s_cached);
    return s_cached;
}

void EspPower::deep_sleep(uint64_t wake_after_us) {
    namespace p = sapiens::fw::pins;
    // Wake when either nav button is pressed (active-low). Hold the pads high
    // through RTC so ANY_LOW triggers reliably during sleep.
    rtc_gpio_pullup_en((gpio_num_t)p::BTN_BOOT);
    rtc_gpio_pulldown_dis((gpio_num_t)p::BTN_BOOT);
    rtc_gpio_pullup_en((gpio_num_t)p::BTN_PWR);
    rtc_gpio_pulldown_dis((gpio_num_t)p::BTN_PWR);
    uint64_t mask = (1ULL << p::BTN_BOOT) | (1ULL << p::BTN_PWR);
    esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
    if (wake_after_us) esp_sleep_enable_timer_wakeup(wake_after_us);
    esp_deep_sleep_start();
}

} // namespace sapiens::fw
