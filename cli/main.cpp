// Headless demo: drives the shared core end-to-end with no graphics, proving the
// Phase-1 domain works. This is the same core the simulator and firmware use.
#include <cstdio>
#include <string>

#include "sapiens/AppController.h"
#include "sapiens/AudioService.h"
#include "sapiens/Id.h"
#include "sapiens/NoteRepository.h"
#include "sapiens/NoteService.h"
#include "sapiens/host/Host.h"

using namespace sapiens;

static void print_notes(const NoteService& notes) {
    auto list = notes.list_notes();
    std::printf("  %zu note(s):\n", list.size());
    for (const auto& n : list) {
        std::printf("   - [%-13s] %-20s v%lld%s\n", to_string(n.status),
                    n.title.c_str(), static_cast<long long>(n.version),
                    n.favorite ? "  ★" : "");
    }
}

int main() {
    host::SystemClock clock;
    host::ConsoleLog log{false};
    host::FakeAudio audio{clock};
    IdGenerator ids{clock};
    InMemoryNoteRepository repo;
    NoteService notes{repo, ids, clock, log};
    AudioService audioSvc{audio, notes, ids, log};
    AppController app{notes, audioSvc, clock, log};

    std::printf("SapiensNote core demo (headless)\n");
    std::printf("screen: %s\n", to_string(app.screen()));

    std::printf("\n1) Record a voice note...\n");
    app.start_recording();
    app.stop_recording(); // FakeAudio yields a ~1s clip
    print_notes(notes);

    std::printf("\n2) Create a typed note + tag it...\n");
    Note typed = notes.create_note("Buy milk");
    Tag errands = notes.create_tag("errands");
    notes.add_tag(typed.id, errands.id);
    notes.set_favorite(typed.id, true);
    print_notes(notes);

    std::printf("\n3) Server returns AI results (via sync)...\n");
    auto recorded = notes.list_notes().back(); // the recording (oldest)
    notes.set_status(recorded.id, NoteStatus::Uploaded);
    notes.set_status(recorded.id, NoteStatus::Transcribing);
    notes.apply_transcription(recorded.id,
                              "Remember to call the dentist tomorrow morning.",
                              "Reminder to call the dentist.",
                              {"call dentist", "tomorrow morning"}, "en");
    print_notes(notes);

    auto got = notes.get_note(recorded.id);
    std::printf("\n   transcript: \"%s\"\n   summary:    \"%s\"\n",
                got->transcript.c_str(), got->summary.c_str());

    std::printf("\n4) Filter favorites...\n");
    NoteFilter favs;
    favs.favorite = true;
    std::printf("  %zu favorite(s)\n", notes.list_notes(favs).size());

    std::printf("\nDone.\n");
    return 0;
}
