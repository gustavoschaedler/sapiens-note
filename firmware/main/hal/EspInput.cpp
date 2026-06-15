#include "EspInput.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "Pins.h"
#include "DeviceSettings.h"

namespace sapiens::fw {
namespace {
const char* TAG = "sapiens.touch";
constexpr int W = 200, H = 200;
constexpr std::uint32_t kDoubleMs = 400; // window to register a 2nd click
constexpr std::uint32_t kDebounceMs = 15;

// Buttons are sampled in a dedicated 5 ms task (the 50 ms UI loop was too coarse to
// reliably catch the brief release between two clicks, so "back" was flaky). The
// task pushes resolved InputKind values to this queue; poll() drains it.
QueueHandle_t s_btn_q = nullptr;

struct Btn {
    gpio_num_t pin;
    bool dbnc{false};       // debounced level (true = pressed)
    bool raw{false};
    std::uint32_t raw_t{0}; // when raw last changed
    bool prev{false};
    int clicks{0};
    std::uint32_t last_click{0};
    std::uint32_t down_ms{0};
    bool long_fired{false};
};

inline void emit(sapiens::InputKind k) {
    int v = (int)k;
    if (s_btn_q) xQueueSend(s_btn_q, &v, 0);
}

void process(Btn& b, std::uint32_t now, sapiens::InputKind single, sapiens::InputKind dbl,
             std::uint32_t longMs, sapiens::InputKind longKind) {
    bool raw = gpio_get_level(b.pin) == 0;
    if (raw != b.raw) { b.raw = raw; b.raw_t = now; }
    if ((now - b.raw_t) >= kDebounceMs) b.dbnc = raw; // commit once stable
    bool down = b.dbnc;
    if (down && !b.prev) { b.prev = true; ++b.clicks; b.last_click = now; b.down_ms = now; b.long_fired = false; }
    if (down && longMs && !b.long_fired && (now - b.down_ms) >= longMs) {
        b.long_fired = true; b.clicks = 0; emit(longKind); // consume; no click on release
    }
    if (!down) { b.prev = false; b.long_fired = false; }
    if (b.clicks >= 2) { b.clicks = 0; emit(dbl); }
    else if (b.clicks == 1 && !down && (now - b.last_click) > kDoubleMs) { b.clicks = 0; emit(single); }
}

void btn_task(void*) {
    using K = sapiens::InputKind;
    Btn top{pins::BTN_BOOT};   // top: 1=next, 2=prev, hold=record
    Btn bot{pins::BTN_PWR};    // bottom: 1=select, 2=back
    for (;;) {
        std::uint32_t now = (std::uint32_t)(esp_timer_get_time() / 1000);
        std::uint32_t hold_ms = (std::uint32_t)settings::hold_secs() * 1000;
        process(top, now, K::FocusNext, K::FocusPrev, hold_ms, K::ButtonRecord);
        process(bot, now, K::FocusSelect, K::Back, 0, K::FocusSelect);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

const char* known_device(uint8_t addr) {
    switch (addr) {
        case 0x38: return "FT6336/FT6236 touch";
        case 0x15: return "CST816/CST816S touch";
        case 0x14: case 0x5D: return "GT911 touch";
        case 0x51: return "PCF85063 RTC";
        case 0x70: return "SHTC3 temp/humidity";
        case 0x18: return "ES8311 audio codec";
        default:   return "unknown";
    }
}
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

bool EspInput::init() {
    namespace p = sapiens::fw::pins;

    // Physical navigation buttons (active-low, internal pull-ups). Sampled by a
    // dedicated task; see btn_task.
    gpio_config_t btn = {};
    btn.intr_type = GPIO_INTR_DISABLE;
    btn.mode = GPIO_MODE_INPUT;
    btn.pin_bit_mask = (1ULL << p::BTN_BOOT) | (1ULL << p::BTN_PWR);
    btn.pull_up_en = GPIO_PULLUP_ENABLE;
    btn.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&btn);
    s_btn_q = xQueueCreate(8, sizeof(int));
    xTaskCreate(btn_task, "sn_btn", 3072, nullptr, 6, nullptr);

    // The audio codec stack (EspAudio::init) may already own the I2C master bus on
    // port 0. Reuse it; only create it if absent.
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    esp_err_t err = i2c_master_get_bus_handle(I2C_NUM_0, &bus_);
    esp_log_level_set("i2c.master", ESP_LOG_WARN);
    if (err != ESP_OK || bus_ == nullptr) {
        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.i2c_port = I2C_NUM_0;
        bus_cfg.sda_io_num = p::I2C_SDA;
        bus_cfg.scl_io_num = p::I2C_SCL;
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.flags.enable_internal_pullup = true;
        err = i2c_new_master_bus(&bus_cfg, &bus_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c bus init failed: %s", esp_err_to_name(err));
            return true;
        }
        ESP_LOGI(TAG, "created I2C bus (no codec present)");
    } else {
        ESP_LOGI(TAG, "reusing codec I2C bus");
    }

    ESP_LOGI(TAG, "I2C scan on SDA=%d SCL=%d ...", p::I2C_SDA, p::I2C_SCL);
    bool touch_present = false;
    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        if (i2c_master_probe(bus_, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "  found 0x%02X  (%s)", addr, known_device(addr));
            if (addr == 0x38) touch_present = true;
        }
    }
    if (!touch_present) {
        ESP_LOGW(TAG, "FT6336 (0x38) not found; touch disabled");
        return true;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = pins::FT6336_ADDR; // 0x38
    dev_cfg.scl_speed_hz = 300000;
    err = i2c_master_bus_add_device(bus_, &dev_cfg, &touch_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add FT6336 failed: %s", esp_err_to_name(err));
        touch_ = nullptr;
        return true;
    }
    ESP_LOGI(TAG, "FT6336 touch ready @0x38");
    return true;
}

std::optional<sapiens::InputEvent> EspInput::poll() {
    // Button events come from the high-rate task via the queue (one per poll).
    if (s_btn_q) {
        int k;
        if (xQueueReceive(s_btn_q, &k, 0) == pdTRUE)
            return sapiens::InputEvent{(sapiens::InputKind)k};
    }

    if (!touch_) return std::nullopt;

    // Read FT6336 registers 0x02 (touch count) .. 0x06 (point #1 X/Y) in one burst.
    uint8_t reg = 0x02;
    uint8_t buf[5] = {0};
    esp_err_t rd = i2c_master_transmit_receive(touch_, &reg, 1, buf, sizeof(buf),
                                               pdMS_TO_TICKS(20));
    if (rd != ESP_OK) {
        static int fails = 0;
        if ((fails++ % 100) == 0)
            ESP_LOGW(TAG, "touch read failed: %s (count %d)", esp_err_to_name(rd), fails);
        return std::nullopt;
    }

    const int n = buf[0] & 0x0F;
    if (n == 0) { was_touched_ = false; return std::nullopt; }
    if (was_touched_) return std::nullopt; // wait for release before next tap
    was_touched_ = true;

    int rx = ((buf[1] & 0x0F) << 8) | buf[2];
    int ry = ((buf[3] & 0x0F) << 8) | buf[4];
    // Direct mapping (FT6336 already reports the 0..199 panel grid).
    constexpr int kRawXMin = 0, kRawXMax = 200;
    constexpr int kRawYMin = 0, kRawYMax = 200;
    int x = clampi((rx - kRawXMin) * (W - 1) / (kRawXMax - kRawXMin), 0, W - 1);
    int y = clampi((ry - kRawYMin) * (H - 1) / (kRawYMax - kRawYMin), 0, H - 1);
    return sapiens::InputEvent{sapiens::InputKind::Tap, x, y};
}

} // namespace sapiens::fw
