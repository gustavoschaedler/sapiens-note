#pragma once
// ILog for ESP-IDF: routes to ESP_LOGx.
#include "esp_log.h"
#include "sapiens/Hal.h"

namespace sapiens::fw {

class EspLog final : public sapiens::ILog {
public:
    void log(sapiens::LogLevel level, const std::string& msg) override {
        static const char* kTag = "sapiens";
        switch (level) {
            case sapiens::LogLevel::Debug: ESP_LOGD(kTag, "%s", msg.c_str()); break;
            case sapiens::LogLevel::Info: ESP_LOGI(kTag, "%s", msg.c_str()); break;
            case sapiens::LogLevel::Warn: ESP_LOGW(kTag, "%s", msg.c_str()); break;
            case sapiens::LogLevel::Error: ESP_LOGE(kTag, "%s", msg.c_str()); break;
        }
    }
};

} // namespace sapiens::fw
