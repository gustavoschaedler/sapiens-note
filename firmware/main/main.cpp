// SapiensNote firmware entry point. Wires the shared core + shared renderer to the
// ESP-IDF HAL. Same code as the simulator except the HAL (see ../README.md and
// ../../docs/SHARED_CORE.md). SCAFFOLD: HAL bodies are marked TODO(board).

#include <dirent.h>
#include <sys/stat.h>
#include <set>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"

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
#include "hal/DeviceSettings.h"
#include "hal/EspClock.h"
#include "hal/EspDashboard.h"
#include "hal/EspDisplay.h"
#include "hal/EspInput.h"
#include "hal/EspLed.h"
#include "hal/EspLog.h"
#include "hal/EspNet.h"
#include "hal/EspPower.h"
#include "hal/EspStorage.h"
#include "hal/Pins.h"
#include "hal/SdNoteRepository.h"

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
        // Scroll/name/sleep are stored in DeviceSettings (the loop applies them to
        // the app); the dashboard edits the same store.
        case T::SpeedDown: sapiens::fw::settings::set_scroll(sapiens::fw::settings::scroll() - 1); break;
        case T::SpeedUp: sapiens::fw::settings::set_scroll(sapiens::fw::settings::scroll() + 1); break;
        case T::SleepDown: sapiens::fw::settings::set_sleep(sapiens::fw::settings::sleep_secs() - 5); break;
        case T::SleepUp: sapiens::fw::settings::set_sleep(sapiens::fw::settings::sleep_secs() + 5); break;
        case T::FormatPrev: sapiens::fw::settings::set_name_fmt(sapiens::fw::settings::name_fmt() - 1); break;
        case T::FormatNext: sapiens::fw::settings::set_name_fmt(sapiens::fw::settings::name_fmt() + 1); break;
        case T::GoNotes: app.go_note_list(); break;
        case T::GoSync: app.go_sync(); break;
        case T::GoTags: app.go_tags(); break;
        case T::SyncNow: app.refresh(); break; // placeholder (no server yet)
        case T::ToggleScroll: app.toggle_transcript_scroll(); break;
        case T::None: break;
    }
    app.refresh();
}

// Draw a 3px focus ring just outside a target's bounds (physical-button nav).
void draw_focus_ring(sim::Canvas& c, const sim::Hit& h) {
    if (h.rx < 0) return;
    for (int i = 0; i < 3; ++i)
        c.draw_round_rect(h.rx - 3 + i, h.ry - 3 + i, h.rw + 6 - 2 * i,
                          h.rh + 6 - 2 * i, 1, 7);
}

// Full-screen status page for the on-demand web/WiFi mode. Shows the standard
// header (brand + battery), the screen title, then the status lines (med font,
// small as a fallback; "" is a spacer).
void draw_status(sim::Canvas& c, int battery, const std::string& title,
                 const std::vector<std::string>& lines) {
    c.clear(0); // white
    c.draw_text(4, 1, "Sapiens Note", 1);
    if (battery >= 0) {
        std::string b = std::to_string(battery) + "%";
        c.draw_text(sim::Canvas::W - 4 - sim::Canvas::text_width(b), 1, b, 1);
    }
    c.hline(0, 18, sim::Canvas::W, 1);
    c.draw_text_med(6, 22, title, 1);
    int y = 50;
    for (const auto& s : lines) {
        if (s.empty()) { y += 11; continue; } // spacer
        if (sim::Canvas::text_width_med(s) <= sim::Canvas::W - 14) {
            c.draw_text_med(8, y, s, 1);
            y += 23;
        } else {
            c.draw_text(8, y + 3, s, 1);
            y += 17;
        }
    }
    c.draw_text(8, sim::Canvas::H - 14, "Back = exit", 1);
}

// Import any WAVs on the card that no note references yet, so older recordings (or
// a note lost before persistence existed) show up in the list. Idempotent: matched
// by audio path, so re-running across boots never duplicates.
void reconcile_audio(sapiens::NoteRepository& repo, NoteService& notes) {
    namespace fw = sapiens::fw;
    std::set<std::string> have;
    NoteFilter f;
    f.include_deleted = true;
    for (const auto& n : repo.list(f))
        if (!n.audio_key.empty()) have.insert(n.audio_key);

    DIR* d = opendir(fw::EspStorage::kAudioDir);
    if (!d) return;
    int imported = 0;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string name = e->d_name;
        if (name.size() < 4 || name.substr(name.size() - 4) != ".wav") continue;
        std::string full = std::string(fw::EspStorage::kAudioDir) + "/" + name;
        if (have.count(full)) continue;
        struct stat st;
        long bytes = (stat(full.c_str(), &st) == 0) ? (long)st.st_size : 0;
        int secs = bytes > 44 ? (int)((bytes - 44) / (16000 * 2)) : 0;
        auto note = notes.create_note(""); // date-time title
        notes.set_audio(note.id, full, secs < 1 ? 1 : secs);
        ++imported;
    }
    closedir(d);
    if (imported) ESP_LOGI(TAG, "reconciled %d orphan recording(s)", imported);
}
// USB mass-storage "disk mode": expose the SD card as a drive on the computer.
// Takes over the USB peripheral, so the normal app/serial don't run; hold the top
// button for 3 s to turn disk mode off and reboot back to normal.
void run_disk_mode() {
    namespace fw = sapiens::fw;
    fw::EspDisplay display;
    display.init();
    bool sd = fw::EspStorage::mount_sd();
    static sim::Canvas canvas;

    if (sd && fw::EspStorage::card()) {
        tinyusb_msc_sdmmc_config_t mcfg = {};
        mcfg.card = (sdmmc_card_t*)fw::EspStorage::card();
        tinyusb_msc_storage_init_sdmmc(&mcfg);
        tinyusb_config_t tcfg = {}; // default descriptors (from Kconfig)
        tinyusb_driver_install(&tcfg);
        draw_status(canvas, -1, "Disk mode",
                    {"This device is now a", "USB drive on your PC.", "",
                     "Recordings (.wav) and", "transcripts (.txt) inside.", "",
                     "Hold TOP button to exit."});
        ESP_LOGI(TAG, "disk mode: USB MSC up");
    } else {
        draw_status(canvas, -1, "Disk mode",
                    {"SD card not found.", "", "Hold TOP button to exit."});
    }
    display.present(canvas.data(), RefreshHint::Full);

    gpio_config_t b = {};
    b.mode = GPIO_MODE_INPUT;
    b.pin_bit_mask = (1ULL << sapiens::fw::pins::BTN_BOOT);
    b.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&b);
    std::uint32_t held = 0;
    for (;;) {
        if (gpio_get_level(sapiens::fw::pins::BTN_BOOT) == 0) {
            held += 100;
            if (held >= 3000) { sapiens::fw::settings::set_disk(false); esp_restart(); }
        } else {
            held = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
} // namespace

extern "C" void app_main(void) {
    // Platform init
    ESP_ERROR_CHECK(nvs_flash_init());
    sapiens::fw::settings::load();       // device settings from NVS
    if (sapiens::fw::settings::disk_mode()) { run_disk_mode(); return; } // USB drive
    sapiens::fw::EspStorage::mount_sd(); // notes/audio live on the SD card

    // HAL
    sapiens::fw::EspClock clock;
    sapiens::fw::EspLog log;
    sapiens::fw::EspAudio audio;
    sapiens::fw::EspDisplay display;
    sapiens::fw::EspInput input;
    sapiens::fw::EspPower power;
    sapiens::fw::EspNet net;
    sapiens::fw::EspDashboard dashboard;
    sapiens::fw::EspLed led;
    // Order matters: display.init() powers the audio rail (GPIO42) and VBAT latch;
    // input.init() creates the shared I2C master bus (port 0). The ES8311 codec in
    // audio.init() then *reuses* that bus, so it must run last.
    display.init();
    input.init();
    audio.init();
    clock.init(); // read PCF85063 RTC into the system clock (needs the I2C bus)
    net.init();   // netif + wifi (idle; connects on demand from the Web screen)
    led.init();   // status-LED blink task (green=USB, red=low battery)

    // Shared core. Notes persist to a JSON index on the SD card so the list
    // survives reboots (audio bytes are the WAVs alongside it).
    IdGenerator ids{clock};
    static sapiens::fw::SdNoteRepository repo{
        std::string(sapiens::fw::EspStorage::kRoot) + "/notes.json"};
    NoteService notes{repo, ids, clock, log};
    AudioService audioSvc{audio, notes, ids, log};
    AppController app{notes, audioSvc, clock, log};

    // Reconcile: import any WAVs on the card that have no note yet (e.g. older
    // recordings, or a note lost before this persisted). Idempotent across boots.
    reconcile_audio(repo, notes);

    // Tell DeviceSettings the valid ranges + the name-format previews (for the
    // dashboard's format dropdown).
    sapiens::fw::settings::set_info(app.scroll_speed_max(), app.name_format_count());
    {
        std::vector<std::string> ex;
        for (int i = 0; i < app.name_format_count(); ++i)
            ex.push_back(app.name_format_example(i));
        sapiens::fw::settings::set_name_examples(ex);
    }

    // Shared renderer drawing the 200x200, 1-bit UI.
    // static (in .bss, not on the stack): Canvas carries a 200x200 = 40 KB
    // framebuffer — far too large for the main-task stack. Keeping it (and the
    // renderer) static lets the main-task stack stay small.
    static sim::Renderer renderer;
    static sim::Canvas canvas;

    ESP_LOGI(TAG, "SapiensNote firmware up; entering UI loop");
    auto last_screen = Screen::Settings; // force first paint
    // e-ink can only refresh a few times/sec, and each refresh blocks ~300 ms, so
    // we poll touch every 50 ms but throttle animated repaints to ~800 ms. Painting
    // every tick made animated screens flash constantly and starved touch input.
    constexpr std::uint32_t kAnimPeriodMs = 800;
    std::uint32_t last_anim_ms = 0;
    int focus_idx = 0; // physical-button cursor over renderer.focus_targets()
    enum class Net { Off, Portal, Connecting, Online, Failed };
    Net net_mode = Net::Off;
    std::string web_text; // last status text rendered (e-ink change detection)
    bool enter_web = false;
    std::uint32_t last_activity = (std::uint32_t)clock.monotonic_ms();
    auto exit_web = [&]() {
        dashboard.stop();
        net.disconnect();
        net.stop_portal();
        net_mode = Net::Off;
        web_text.clear();
        last_activity = (std::uint32_t)clock.monotonic_ms();
    };

    while (true) {
        if (sapiens::fw::settings::disk_mode()) esp_restart(); // enter disk mode

        bool acted = false;
        if (auto ev = input.poll()) {
            acted = true;
            if (net_mode != Net::Off) {
                // In web mode only Back is honoured (it tears everything down).
                if (ev->kind == InputKind::Back) exit_web();
            } else {
                auto activate = [&](const sim::Hit& h) {
                    apply_hit(app, h);
                    if (h.type == sim::Hit::Type::SyncNow) enter_web = true;
                };
                switch (ev->kind) {
                    case InputKind::Tap:
                        activate(renderer.hittest(ev->x, ev->y, app));
                        focus_idx = 0;
                        break;
                    case InputKind::Back:
                        app.back();
                        app.refresh();
                        focus_idx = 0;
                        break;
                    case InputKind::FocusNext: {
                        auto tg = renderer.focus_targets(app);
                        if (!tg.empty()) focus_idx = (focus_idx + 1) % (int)tg.size();
                        break;
                    }
                    case InputKind::FocusPrev: {
                        auto tg = renderer.focus_targets(app);
                        if (!tg.empty())
                            focus_idx = (focus_idx - 1 + (int)tg.size()) % (int)tg.size();
                        break;
                    }
                    case InputKind::FocusSelect: {
                        auto tg = renderer.focus_targets(app);
                        if (!tg.empty() && focus_idx < (int)tg.size()) activate(tg[focus_idx]);
                        // Keep the cursor on the same item; a screen change resets it
                        // below (so steppers/toggles don't make the highlight jump).
                        break;
                    }
                    default:
                        app.handle(*ev);
                        break;
                }
            }
        }

        // --- On-demand WiFi / web mode --------------------------------------
        if (enter_web) {
            enter_web = false;
            net_mode = net.has_credentials() ? Net::Connecting : Net::Portal;
            if (net_mode == Net::Portal) net.start_portal();
            web_text.clear(); // force a status repaint
        }
        if (net_mode != Net::Off) {
            std::string title = "Wi-Fi";
            const int bat = power.battery_percent();
            std::vector<std::string> lines;
            if (net_mode == Net::Portal) {
                lines = {"Connect your phone to:", net.ap_ssid(), "",
                         "The page opens", "automatically, or go to:", "192.168.4.1"};
                if (net.provisioned()) { net.stop_portal(); net_mode = Net::Connecting; web_text.clear(); }
            }
            if (net_mode == Net::Connecting) {
                // Show "connecting" first, then block on the (slow) connect.
                if (web_text != "connecting") {
                    draw_status(canvas, bat, title, {"Connecting to Wi-Fi...", "please wait"});
                    display.present(canvas.data(), RefreshHint::Full);
                    web_text = "connecting";
                    if (net.connect()) {
                        if (net.sync_ntp()) clock.write_system_to_rtc();
                        dashboard.start(notes); // serve the web dashboard on :80
                        net_mode = Net::Online;
                    } else {
                        net_mode = Net::Failed;
                    }
                    web_text.clear();
                }
            }
            if (net_mode == Net::Online)
                lines = {"Dashboard ready.", "", "Open in a browser:",
                         "http://" + net.ip(), "", "Time synchronised."};
            if (net_mode == Net::Failed)
                lines = {"Could not connect.", "", "Check the network & password."};

            if (net_mode != Net::Connecting) {
                std::string key = title;
                for (auto& l : lines) key += "|" + l;
                if (key != web_text) {
                    draw_status(canvas, bat, title, lines);
                    display.present(canvas.data(), RefreshHint::Full);
                    web_text = key;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(net_mode == Net::Portal ? 200 : 100));
            continue; // skip the normal app render while in web mode
        }

        // Apply scroll/name from DeviceSettings (source of truth; dashboard edits it).
        app.set_scroll_speed(sapiens::fw::settings::scroll());
        app.set_name_format(sapiens::fw::settings::name_fmt());

        led.set_battery(power.battery_percent()); // feed the low-battery indicator

        app.maybe_advance_splash(); // boot splash -> Home after 4s
        if (app.screen() == Screen::NoteDetail)
            app.tick_transcript_scroll(50); // ~loop period; auto-scroll the transcript

        const auto now = (std::uint32_t)clock.monotonic_ms();
        const bool screen_changed = (app.screen() != last_screen);
        if (screen_changed) focus_idx = 0; // restart the cursor on each new screen
        if (acted || screen_changed) last_activity = now; // anything = activity
        const bool animated = (app.screen() == Screen::Recording) ||
                              (app.screen() == Screen::NoteDetail) ||
                              (app.screen() == Screen::Player) ||
                              (app.screen() == Screen::NoteList) || // marquee the long titles
                              (app.screen() == Screen::Splash);
        const bool anim_due = animated && (now - last_anim_ms >= kAnimPeriodMs);

        // Repaint on a tap, a screen change, or a throttled animation tick.
        if (acted || screen_changed || anim_due) {
            renderer.render(canvas, app, power.battery_percent(), now,
                            sapiens::fw::settings::sleep_secs());
            // Highlight the focused target so the physical buttons have a visible
            // cursor (touch still works; the ring just shows where "select" lands).
            auto tg = renderer.focus_targets(app);
            if (!tg.empty()) {
                if (focus_idx >= (int)tg.size()) focus_idx = 0;
                draw_focus_ring(canvas, tg[focus_idx]);
            }
            display.present(canvas.data(),
                            screen_changed ? RefreshHint::Full : RefreshHint::Partial);
            last_screen = app.screen();
            last_anim_ms = now;
        }

        // Idle -> deep sleep (skip on the Sync screen, while recording, or playing).
        // Timeout is configurable in the dashboard Settings; 0 disables it.
        const int sleep_s = sapiens::fw::settings::sleep_secs();
        const bool sleepable = sleep_s > 0 &&
                               !usb_serial_jtag_is_connected() && // stay awake on USB (dev/charge)
                               app.screen() != Screen::Sync &&
                               app.screen() != Screen::Recording &&
                               !(app.screen() == Screen::Player && app.audio_playing());
        if (sleepable && (now - last_activity) > (std::uint32_t)sleep_s * 1000) {
            draw_status(canvas, power.battery_percent(), "Sleeping",
                        {"No activity.", "", "Entering deep sleep.",
                         "Press any button to wake."});
            display.present(canvas.data(), RefreshHint::Full);
            vTaskDelay(pdMS_TO_TICKS(1500));
            sapiens::fw::EspPower::deep_sleep();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
        // TODO(board): after an idle timeout, EspPower::deep_sleep() and wake on
        // touch INT / PWR button / RTC alarm (offline-first; e-ink holds image).
    }
}
