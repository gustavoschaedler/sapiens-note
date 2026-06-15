#include "DeviceSettings.h"

#include <atomic>

#include "nvs.h"
#include "esp_log.h"

static std::vector<std::string> s_name_examples;

namespace sapiens::fw::settings {
namespace {
const char* TAG = "sapiens.cfg";
constexpr char NS[] = "dev";
std::atomic<int> s_sleep{15};
std::atomic<int> s_hold{3};
std::atomic<int> s_scroll{2};
std::atomic<int> s_name{0};
std::atomic<int> s_scroll_max{5};
std::atomic<int> s_name_count{1};
std::atomic<bool> s_disk{false};

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int wrapi(int v, int n) { return n > 0 ? ((v % n) + n) % n : 0; }

void put_i32(const char* k, int v) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, k, v);
    nvs_commit(h);
    nvs_close(h);
}
} // namespace

void load() {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    int32_t v;
    if (nvs_get_i32(h, "sleep_s", &v) == ESP_OK) s_sleep = clampi(v, 0, 3600);
    if (nvs_get_i32(h, "hold_s", &v) == ESP_OK) s_hold = clampi(v, 1, 10);
    if (nvs_get_i32(h, "scroll", &v) == ESP_OK) s_scroll = v;
    if (nvs_get_i32(h, "name", &v) == ESP_OK) s_name = v;
    if (nvs_get_i32(h, "disk", &v) == ESP_OK) s_disk = (v != 0);
    nvs_close(h);
    ESP_LOGI(TAG, "loaded sleep=%d hold=%d scroll=%d name=%d",
             s_sleep.load(), s_hold.load(), s_scroll.load(), s_name.load());
}

void set_info(int scroll_max, int name_count) {
    if (scroll_max > 0) s_scroll_max = scroll_max;
    if (name_count > 0) s_name_count = name_count;
    s_scroll = clampi(s_scroll.load(), 1, s_scroll_max.load());
    s_name = wrapi(s_name.load(), s_name_count.load());
}

int sleep_secs() { return s_sleep.load(); }
int hold_secs() { return s_hold.load(); }
int scroll() { return s_scroll.load(); }
int scroll_max() { return s_scroll_max.load(); }
int name_fmt() { return s_name.load(); }
int name_count() { return s_name_count.load(); }

void set_name_examples(const std::vector<std::string>& ex) { s_name_examples = ex; }
const std::vector<std::string>& name_examples() { return s_name_examples; }

void set_sleep(int v) { s_sleep = clampi(v, 0, 3600); put_i32("sleep_s", s_sleep.load()); }
void set_hold(int v) { s_hold = clampi(v, 1, 10); put_i32("hold_s", s_hold.load()); }
void set_scroll(int v) { s_scroll = clampi(v, 1, s_scroll_max.load()); put_i32("scroll", s_scroll.load()); }
void set_name_fmt(int v) { s_name = wrapi(v, s_name_count.load()); put_i32("name", s_name.load()); }
bool disk_mode() { return s_disk.load(); }
void set_disk(bool on) { s_disk = on; put_i32("disk", on ? 1 : 0); }

} // namespace sapiens::fw::settings
