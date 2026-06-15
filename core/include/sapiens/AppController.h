#pragma once
// The shared app state machine + navigation, driven by input events. Both the
// firmware and the simulator render whatever AppController says is current.
// See docs/UI_UX.md for the screen graph.

#include <optional>
#include <string>
#include <vector>

#include "sapiens/AudioService.h"
#include "sapiens/Hal.h"
#include "sapiens/NoteService.h"
#include "sapiens/Types.h"

namespace sapiens {

enum class Screen { Splash, Home, NoteList, Recording, NoteDetail, Settings,
                    Player, ConfirmDelete, Sync, Tags };

const char* to_string(Screen s);

class AppController {
public:
    AppController(NoteService& notes, AudioService& audio, IClock& clock, ILog& log);

    Screen screen() const { return screen_; }
    const std::vector<Note>& visible_notes() const { return visible_; }
    const std::optional<Note>& selected_note() const { return selected_; }
    int recording_elapsed_secs() const; // valid while on Recording screen

    void refresh();                 // reload visible_ from the service
    void reload_selected();         // re-fetch the open note (e.g. after a sync)

    // Transcript vertical scroll (NoteDetail). A monotonic "scroll clock" the
    // renderer turns into an offset; tapping the text pauses/resumes it.
    std::int64_t transcript_scroll_ms() const { return transcript_scroll_ms_; }
    bool transcript_paused() const { return transcript_paused_; }
    void tick_transcript_scroll(int dt_ms) {
        if (!transcript_paused_ && dt_ms > 0) transcript_scroll_ms_ += dt_ms;
    }
    void toggle_transcript_scroll() { transcript_paused_ = !transcript_paused_; }
    void reset_transcript_scroll() { transcript_scroll_ms_ = 0; transcript_paused_ = false; }
    void handle(const InputEvent& e); // advance the state machine

    // Screen transitions (also callable directly by a renderer's buttons).
    void go_note_list();
    void go_home();
    void go_sync();
    void go_tags();
    void start_recording();
    void stop_recording();
    void open_note(const std::string& id);
    void open_settings();
    void back(); // context-aware

    // Splash: shown on boot for splash_duration_ms(), then auto-advances to Home.
    void show_splash();                 // (re)enter the splash screen
    int splash_duration_ms() const { return splash_duration_ms_; }
    int splash_elapsed_ms() const;      // time since the splash began
    bool maybe_advance_splash();        // call each frame; true if it advanced
    // Test/screenshot helper: pretend the splash began `ms` ago.
    void debug_set_splash_elapsed(int ms);

    // Tags (read-only view for the Tags screen).
    std::vector<Tag> tags() const { return notes_.list_tags(); }

    // Delete with confirmation.
    void request_delete();  // Detail -> ConfirmDelete
    void confirm_delete();  // delete the selected note -> List
    void cancel_delete();   // ConfirmDelete -> Detail

    // Name format (Settings). 5 date-time formats.
    int name_format() const;
    int name_format_count() const;
    void cycle_name_format();             // advance to the next format
    void set_name_format(int fmt);
    std::string name_format_preview() const;       // current format applied to "now"
    std::string name_format_example(int i) const;  // format i applied to "now"

    // List pagination (fixed 3 notes per page — big rows on the e-paper).
    int page_size() const { return 3; }
    int total_notes() const { return static_cast<int>(visible_.size()); }
    int page_count() const;               // >= 1
    int list_page() const { return list_page_; }      // 0-based
    int page_first_index() const { return list_page_ * page_size(); } // for counter
    std::vector<Note> page_notes() const; // current page slice
    void page_up();                       // previous page
    void page_down();                     // next page

    // Marquee scroll speed (1 = slow .. 5 = fast), recalibrated slower.
    int scroll_speed() const { return scroll_speed_; }
    int scroll_speed_min() const { return 1; }
    int scroll_speed_max() const { return 5; }
    void set_scroll_speed(int s);

    // Player
    void open_player();          // open the player for the selected note + start
    void play_selected();        // start/restart playback of the selected note
    void toggle_play();          // play/pause
    void stop_play();
    bool has_audio() const;
    bool audio_playing() const;
    bool audio_paused() const;
    int audio_pos_secs() const;
    int audio_total_secs() const;
    float audio_progress() const;

private:
    NoteService& notes_;
    AudioService& audio_;
    IClock& clock_;
    ILog& log_;

    Screen screen_{Screen::Splash};
    std::vector<Note> visible_;
    std::optional<Note> selected_;
    std::int64_t recording_started_ms_{0};
    std::int64_t splash_started_ms_{0};
    int splash_duration_ms_{4000};
    std::int64_t transcript_scroll_ms_{0};
    bool transcript_paused_{false};
    int list_page_{0};
    int scroll_speed_{2};
};

} // namespace sapiens
