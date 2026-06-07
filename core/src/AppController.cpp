#include "sapiens/AppController.h"

#include <algorithm>

#include "sapiens/Format.h"

namespace sapiens {

const char* to_string(Screen s) {
    switch (s) {
        case Screen::Splash: return "Splash";
        case Screen::Home: return "Home";
        case Screen::NoteList: return "NoteList";
        case Screen::Recording: return "Recording";
        case Screen::NoteDetail: return "NoteDetail";
        case Screen::Settings: return "Settings";
        case Screen::Player: return "Player";
        case Screen::ConfirmDelete: return "ConfirmDelete";
        case Screen::Sync: return "Sync";
        case Screen::Tags: return "Tags";
    }
    return "unknown";
}

AppController::AppController(NoteService& notes, AudioService& audio, IClock& clock,
                            ILog& log)
    : notes_(notes), audio_(audio), clock_(clock), log_(log) {
    splash_started_ms_ = clock_.monotonic_ms();
    refresh();
}

void AppController::refresh() {
    visible_ = notes_.list_notes();
    int pc = page_count();
    if (list_page_ >= pc) list_page_ = pc - 1;
    if (list_page_ < 0) list_page_ = 0;
}

void AppController::reload_selected() {
    if (selected_) {
        if (auto fresh = notes_.get_note(selected_->id)) selected_ = fresh;
    }
}

int AppController::page_count() const {
    int n = total_notes();
    int ps = page_size();
    return n <= 0 ? 1 : (n + ps - 1) / ps;
}

std::vector<Note> AppController::page_notes() const {
    std::vector<Note> out;
    int start = list_page_ * page_size();
    for (int i = start; i < start + page_size() && i < (int)visible_.size(); ++i)
        out.push_back(visible_[i]);
    return out;
}

void AppController::page_up() {
    if (list_page_ > 0) --list_page_;
}
void AppController::page_down() {
    if (list_page_ < page_count() - 1) ++list_page_;
}

void AppController::set_scroll_speed(int s) {
    scroll_speed_ = std::clamp(s, scroll_speed_min(), scroll_speed_max());
}

int AppController::recording_elapsed_secs() const {
    if (screen_ != Screen::Recording) return 0;
    return static_cast<int>((clock_.monotonic_ms() - recording_started_ms_) / 1000);
}

void AppController::go_note_list() {
    screen_ = Screen::NoteList;
    selected_.reset();
    refresh();
}

void AppController::go_home() {
    screen_ = Screen::Home;
    selected_.reset();
}

void AppController::go_sync() { screen_ = Screen::Sync; }
void AppController::go_tags() { screen_ = Screen::Tags; }

void AppController::show_splash() {
    screen_ = Screen::Splash;
    splash_started_ms_ = clock_.monotonic_ms();
}

int AppController::splash_elapsed_ms() const {
    return static_cast<int>(clock_.monotonic_ms() - splash_started_ms_);
}

void AppController::debug_set_splash_elapsed(int ms) {
    splash_started_ms_ = clock_.monotonic_ms() - ms;
}

bool AppController::maybe_advance_splash() {
    if (screen_ != Screen::Splash) return false;
    if (clock_.monotonic_ms() - splash_started_ms_ >= splash_duration_ms_) {
        go_home();
        return true;
    }
    return false;
}

void AppController::start_recording() {
    audio_.start();
    recording_started_ms_ = clock_.monotonic_ms();
    screen_ = Screen::Recording;
}

void AppController::stop_recording() {
    auto note = audio_.stop_and_create();
    if (note) log_.info("note from recording: " + note->id);
    go_note_list();
}

void AppController::open_note(const std::string& id) {
    selected_ = notes_.get_note(id);
    if (selected_) {
        screen_ = Screen::NoteDetail;
        reset_transcript_scroll(); // each note starts at the top, not paused
    }
}

void AppController::open_settings() { screen_ = Screen::Settings; }

void AppController::back() {
    switch (screen_) {
        case Screen::Player:
            audio_.stop();
            screen_ = Screen::NoteDetail;
            break;
        case Screen::ConfirmDelete:
            screen_ = Screen::NoteDetail;
            break;
        case Screen::NoteDetail:
            go_note_list();
            break;
        case Screen::NoteList:
        case Screen::Settings:
        case Screen::Sync:
        case Screen::Tags:
            go_home();
            break;
        default:
            go_home();
            break;
    }
}

void AppController::request_delete() {
    if (selected_) screen_ = Screen::ConfirmDelete;
}

void AppController::confirm_delete() {
    if (selected_) {
        audio_.stop();
        notes_.soft_delete(selected_->id);
    }
    go_note_list();
}

void AppController::cancel_delete() { screen_ = Screen::NoteDetail; }

int AppController::name_format() const { return notes_.name_format(); }
int AppController::name_format_count() const { return fmt::kNameFormatCount; }
void AppController::cycle_name_format() {
    notes_.set_name_format((notes_.name_format() + 1) % fmt::kNameFormatCount);
}
void AppController::set_name_format(int f) { notes_.set_name_format(f); }
std::string AppController::name_format_preview() const {
    return fmt::format_timestamp(clock_.now_ms(), notes_.name_format());
}
std::string AppController::name_format_example(int i) const {
    return fmt::format_timestamp(clock_.now_ms(), i);
}

void AppController::open_player() {
    if (!has_audio()) return;
    screen_ = Screen::Player;
    audio_.play(selected_->audio_key);
}

void AppController::play_selected() {
    if (has_audio()) audio_.play(selected_->audio_key);
}

void AppController::toggle_play() {
    if (!has_audio()) return;
    // If nothing is loaded/finished, (re)start; otherwise pause/resume.
    if (!audio_.is_playing() && !audio_.is_paused())
        audio_.play(selected_->audio_key);
    else
        audio_.pause_resume();
}

void AppController::stop_play() { audio_.stop(); }

bool AppController::has_audio() const {
    return selected_ && !selected_->audio_key.empty();
}
bool AppController::audio_playing() const { return audio_.is_playing(); }
bool AppController::audio_paused() const { return audio_.is_paused(); }
int AppController::audio_pos_secs() const { return audio_.pos_secs(); }
int AppController::audio_total_secs() const { return audio_.total_secs(); }
float AppController::audio_progress() const { return audio_.progress(); }

void AppController::handle(const InputEvent& e) {
    switch (e.kind) {
        case InputKind::ButtonRecord:
            if (screen_ == Screen::Recording) stop_recording();
            else start_recording();
            break;
        case InputKind::Back:
        case InputKind::SwipeLeft:
            back();
            break;
        default:
            // Tap handling on regions is done by the renderer, which calls the
            // explicit transition methods above with the resolved target.
            break;
    }
}

} // namespace sapiens
