#include "EspDisplay.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "Pins.h"
#include "epaper_driver_bsp.h"
#include "board_power_bsp.h"

namespace sapiens::fw {
namespace {
const char* TAG = "sapiens.epd";
constexpr int W = 200, H = 200;
constexpr int BPR = (W + 7) / 8;        // 25 bytes per row, MSB-first
constexpr int kBufLen = BPR * H;        // 5000 bytes
// Full refresh every N partials to clear e-ink ghosting. Kept high so animated
// screens (timer/progress) rarely trigger the black full-screen flash; a screen
// change always does a full refresh anyway (RefreshHint::Full). At ~800 ms/partial
// this is a full clear roughly once a minute on animated screens.
constexpr int kFullEvery = 75;
} // namespace

bool EspDisplay::init() {
    namespace p = sapiens::fw::pins;

    // 1) Power the rails. EPD/audio are active-LOW, the VBAT latch is active-HIGH
    //    (so the board stays on when running from the battery).
    power_ = new board_power_bsp_t(p::EPD_PWR, p::AUDIO_PWR, p::VBAT_PWR);
    power_->VBAT_POWER_ON();   // hold the battery power latch
    power_->POWEER_EPD_ON();   // energize the e-paper rail
    power_->POWEER_Audio_ON(); // energize the audio rail (codec bring-up later)
    vTaskDelay(pdMS_TO_TICKS(200));

    // 2) Bring up the SPI panel driver with the confirmed pin map.
    custom_lcd_spi_t cfg = {};
    cfg.cs   = p::EPD_CS;
    cfg.dc   = p::EPD_DC;
    cfg.rst  = p::EPD_RST;
    cfg.busy = p::EPD_BUSY;
    cfg.mosi = p::EPD_MOSI;
    cfg.scl  = p::EPD_SCK;
    cfg.spi_host = p::EPD_SPI_HOST;
    cfg.buffer_len = kBufLen;
    panel_ = new epaper_driver_display(W, H, cfg);

    // 3) Full init + clear to white, set the partial-refresh base image, then
    //    switch to partial mode (fast updates with no full-screen flash).
    panel_->EPD_Init();
    panel_->EPD_Clear();              // buffer = 0xFF (white)
    panel_->EPD_DisplayPartBaseImage();
    panel_->EPD_Init_Partial();
    partials_since_full_ = 0;

    ESP_LOGI(TAG, "e-paper up: 200x200, SPI2 (DC=%d CS=%d SCK=%d MOSI=%d RST=%d BUSY=%d)",
             p::EPD_DC, p::EPD_CS, p::EPD_SCK, p::EPD_MOSI, p::EPD_RST, p::EPD_BUSY);
    return true;
}

void EspDisplay::blit(const std::uint8_t* pixels) {
    // Sapiens canvas: 1 byte/pixel, >=128 means black (ink).
    // Panel buffer: 1bpp MSB-first, bit=1 -> white, bit=0 -> black.
    std::uint8_t* buf = panel_->getBuffer();
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int idx = y * BPR + (x >> 3);
            const std::uint8_t bit = static_cast<std::uint8_t>(0x80 >> (x & 7));
            if (pixels[y * W + x] >= 128)
                buf[idx] &= static_cast<std::uint8_t>(~bit); // black
            else
                buf[idx] |= bit;                              // white
        }
    }
}

void EspDisplay::full_refresh() {
    // Re-run the full waveform to wipe accumulated ghosting, then re-arm partial
    // mode. The current buffer is shown as the new base image.
    panel_->EPD_Init();
    panel_->EPD_DisplayPartBaseImage();
    panel_->EPD_Init_Partial();
    partials_since_full_ = 0;
}

void EspDisplay::present(const std::uint8_t* pixels, sapiens::RefreshHint hint) {
    if (!panel_) return;
    blit(pixels);

    const bool full = (hint == sapiens::RefreshHint::Full) ||
                      (++partials_since_full_ >= kFullEvery);
    if (full)
        full_refresh();
    else
        panel_->EPD_DisplayPart();
}

} // namespace sapiens::fw
