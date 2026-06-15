#include "EspClock.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "nvs.h"
#include "esp_log.h"
#include "Pins.h"

// Host wall-clock at build time (local digits), regenerated before each build by
// the flash workflow. Lets a fresh flash seed the RTC with the real time once.
#if __has_include("BuildClock.h")
#include "BuildClock.h"
#endif
#ifndef SN_BUILD_TOKEN
#define SN_BUILD_Y 2026
#define SN_BUILD_MO 1
#define SN_BUILD_D 1
#define SN_BUILD_H 0
#define SN_BUILD_MI 0
#define SN_BUILD_S 0
#define SN_BUILD_TOKEN "00000000000000"
#endif

namespace sapiens::fw {
namespace {
const char* TAG = "sapiens.rtc";

constexpr uint8_t REG_CTRL1 = 0x00;
constexpr uint8_t REG_SECONDS = 0x04;

inline int bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
inline uint8_t dec2bcd(int d) { return (uint8_t)(((d / 10) << 4) | (d % 10)); }

std::tm build_seed_tm() {
    std::tm t = {};
    t.tm_year = SN_BUILD_Y - 1900;
    t.tm_mon = SN_BUILD_MO - 1;
    t.tm_mday = SN_BUILD_D;
    t.tm_hour = SN_BUILD_H;
    t.tm_min = SN_BUILD_MI;
    t.tm_sec = SN_BUILD_S;
    return t;
}

void apply_system_time(const std::tm& tm_in) {
    std::tm tmp = tm_in;
    time_t epoch = mktime(&tmp);
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
}

void write_rtc(i2c_master_dev_handle_t rtc, const std::tm& t) {
    uint8_t ctrl[2] = { REG_CTRL1, 0x00 }; // oscillator running
    i2c_master_transmit(rtc, ctrl, sizeof(ctrl), pdMS_TO_TICKS(50));
    uint8_t w[8] = {
        REG_SECONDS,
        dec2bcd(t.tm_sec), dec2bcd(t.tm_min), dec2bcd(t.tm_hour),
        dec2bcd(t.tm_mday), 0 /*weekday*/, dec2bcd(t.tm_mon + 1),
        dec2bcd((t.tm_year + 1900) - 2000),
    };
    i2c_master_transmit(rtc, w, sizeof(w), pdMS_TO_TICKS(50));
}

// One-shot: have we already applied this build's seed? (so reboots keep ticking)
bool seed_already_applied() {
    nvs_handle_t h;
    if (nvs_open("sapiens", NVS_READONLY, &h) != ESP_OK) return false;
    char stored[20] = {0};
    size_t len = sizeof(stored);
    esp_err_t e = nvs_get_str(h, "rtcseed", stored, &len);
    nvs_close(h);
    return e == ESP_OK && std::strcmp(stored, SN_BUILD_TOKEN) == 0;
}
void mark_seed_applied() {
    nvs_handle_t h;
    if (nvs_open("sapiens", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "rtcseed", SN_BUILD_TOKEN);
    nvs_commit(h);
    nvs_close(h);
}
} // namespace

void EspClock::init() {
    i2c_master_bus_handle_t bus = nullptr;
    if (i2c_master_get_bus_handle(I2C_NUM_0, &bus) != ESP_OK || !bus) {
        ESP_LOGW(TAG, "no I2C bus; RTC skipped");
        return;
    }
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = sapiens::fw::pins::PCF85063_ADDR; // 0x51
    dev_cfg.scl_speed_hz = 300000;
    i2c_master_dev_handle_t rtc = nullptr;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &rtc) != ESP_OK || !rtc) {
        ESP_LOGW(TAG, "PCF85063 add failed; RTC skipped");
        return;
    }

    // Read the current RTC value (for the valid-check and logging).
    uint8_t reg = REG_SECONDS, buf[7] = {0};
    bool read_ok = i2c_master_transmit_receive(rtc, &reg, 1, buf, sizeof(buf),
                                               pdMS_TO_TICKS(50)) == ESP_OK;
    std::tm t = {};
    bool rtc_valid = false;
    if (read_ok) {
        bool osc_stopped = buf[0] & 0x80;
        t.tm_sec = bcd2dec(buf[0] & 0x7F);
        t.tm_min = bcd2dec(buf[1] & 0x7F);
        t.tm_hour = bcd2dec(buf[2] & 0x3F);
        t.tm_mday = bcd2dec(buf[3] & 0x3F);
        t.tm_mon = bcd2dec(buf[5] & 0x1F) - 1;
        t.tm_year = 2000 + bcd2dec(buf[6]) - 1900;
        int year = t.tm_year + 1900;
        rtc_valid = !osc_stopped && year >= 2024 && year < 2099;
    }

    if (!seed_already_applied()) {
        // Fresh flash of this build → set the RTC to the host's build-time wall clock.
        t = build_seed_tm();
        write_rtc(rtc, t);
        mark_seed_applied();
        ESP_LOGW(TAG, "RTC synced to build time %04d-%02d-%02d %02d:%02d:%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    } else if (!rtc_valid) {
        t = build_seed_tm();
        write_rtc(rtc, t);
        ESP_LOGW(TAG, "RTC was invalid; reseeded to build time");
    } else {
        ESP_LOGI(TAG, "RTC %04d-%02d-%02d %02d:%02d:%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    }

    apply_system_time(t);
    i2c_master_bus_rm_device(rtc);
}

bool EspClock::write_system_to_rtc() {
    i2c_master_bus_handle_t bus = nullptr;
    if (i2c_master_get_bus_handle(I2C_NUM_0, &bus) != ESP_OK || !bus) return false;
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = sapiens::fw::pins::PCF85063_ADDR;
    dev_cfg.scl_speed_hz = 300000;
    i2c_master_dev_handle_t rtc = nullptr;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &rtc) != ESP_OK || !rtc) return false;

    time_t now = ::time(nullptr);
    std::tm tmv;
    gmtime_r(&now, &tmv); // TZ unset -> system time is already the wall clock we use
    write_rtc(rtc, tmv);
    i2c_master_bus_rm_device(rtc);
    ESP_LOGI(TAG, "RTC updated from system clock %04d-%02d-%02d %02d:%02d:%02d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return true;
}

} // namespace sapiens::fw
