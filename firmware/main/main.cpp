// SapiensNote firmware entry point. Wires the shared core + shared renderer to the
// ESP-IDF HAL. Same code as the simulator except the HAL (see ../README.md and
// ../../docs/SHARED_CORE.md). SCAFFOLD: HAL bodies are marked TODO(board).

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

// Shared core
#include "sapiens/AppController.h"
#include "sapiens/AudioService.h"
#include "sapiens/Id.h"
#include "sapiens/NoteRepository.h"
#include "sapiens/NoteService.h"

// Shared, portable renderer (same files the simulator uses)
#include "Canvas.h"
#include "Renderer.h"

// ESP-IDF HAL
#include "hal/EspAudio.h"
#include "hal/EspClock.h"
#include "hal/EspDisplay.h"
#include "hal/EspInput.h"
#include "hal/EspLog.h"
#include "hal/EspPower.h"
#include "hal/EspStorage.h"

using namespace sapiens;

namespace {
const char* TAG = "sapiens.main";

void apply_hit(AppController& app, const sim::Hit& h) {
    using T = sim::Hit::Type;
    switch (h.type) {
        case T::Record: app.handle(InputEvent{InputKind::ButtonRecord}); break;
        case T::OpenNote: app.open_note(h.note_id); break;
        case T::Back: app.back(); break;
        case T::Settings: app.open_settings(); break;
        case T::OpenPlayer: app.open_player(); break;
        case T::PlayPause: app.toggle_play(); break;
        case T::Stop: app.stop_play(); break;
        case T::Delete: app.request_delete(); break;
        case T::ConfirmDelete: app.confirm_delete(); break;
        case T::CancelDelete: app.cancel_delete(); break;
        case T::PageUp: app.page_up(); break;
        case T::PageDown: app.page_down(); break;
        case T::SpeedDown: app.set_scroll_speed(app.scroll_speed() - 1); break;
        case T::SpeedUp: app.set_scroll_speed(app.scroll_speed() + 1); break;
        case T::FormatPrev: { int n = app.name_format_count(); app.set_name_format((app.name_format() + n - 1) % n); break; }
        case T::FormatNext: { int n = app.name_format_count(); app.set_name_format((app.name_format() + 1) % n); break; }
        case T::GoNotes: app.go_note_list(); break;
        case T::GoSync: app.go_sync(); break;
        case T::GoTags: app.go_tags(); break;
        case T::SyncNow: app.refresh(); break; // placeholder (no server yet)
        case T::ToggleScroll: app.toggle_transcript_scroll(); break;
        case T::None: break;
    }
    app.refresh();
}
} // namespace

extern "C" void app_main(void) {
    // Platform init
    ESP_ERROR_CHECK(nvs_flash_init());
    sapiens::fw::EspStorage::mount_sd(); // notes/audio live on the SD card

    // HAL
    sapiens::fw::EspClock clock;
    sapiens::fw::EspLog log;
    sapiens::fw::EspAudio audio;
    sapiens::fw::EspDisplay display;
    sapiens::fw::EspInput input;
    sapiens::fw::EspPower power;
    audio.init();
    display.init();
    input.init();

    // Shared core (TODO: swap InMemory -> SQLite-on-SD once storage is wired)
    IdGenerator ids{clock};
    InMemoryNoteRepository repo;
    NoteService notes{repo, ids, clock, log};
    AudioService audioSvc{audio, notes, ids, log};
    AppController app{notes, audioSvc, clock, log};

    // Shared renderer drawing the 200x200, 1-bit UI
    sim::Renderer renderer;
    sim::Canvas canvas;

    ESP_LOGI(TAG, "SapiensNote firmware up; entering UI loop");
    auto last_screen = Screen::Settings; // force first paint
    while (true) {
        if (auto ev = input.poll()) {
            if (ev->kind == InputKind::Tap)
                apply_hit(app, renderer.hittest(ev->x, ev->y, app));
            else
                app.handle(*ev);
        }

        app.maybe_advance_splash(); // boot splash -> Home after 4s
        if (app.screen() == Screen::NoteDetail)
            app.tick_transcript_scroll(50); // ~loop period; auto-scroll the transcript

        // Repaint on change (recording/detail/player animate each tick).
        bool dirty = (app.screen() != last_screen) ||
                     (app.screen() == Screen::Recording) ||
                     (app.screen() == Screen::NoteDetail) ||
                     (app.screen() == Screen::Player);
        if (dirty) {
            renderer.render(canvas, app, power.battery_percent(),
                            (std::uint32_t)clock.monotonic_ms());
            display.present(canvas.data(),
                            app.screen() != last_screen ? RefreshHint::Full
                                                        : RefreshHint::Partial);
            last_screen = app.screen();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
        // TODO(board): after an idle timeout, EspPower::deep_sleep() and wake on
        // touch INT / PWR button / RTC alarm (offline-first; e-ink holds image).
    }
}
