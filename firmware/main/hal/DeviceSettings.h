#pragma once
// NVS-backed device settings, the single source of truth for the on-device
// Settings screen AND the web dashboard. The UI loop applies scroll/name to the
// AppController each tick; sleep is read by the loop, hold by the input HAL.
#include <string>
#include <vector>

namespace sapiens::fw::settings {

void load();                              // load from NVS into the cache (boot)
void set_info(int scroll_max, int name_count); // valid ranges from the core (boot)
void set_name_examples(const std::vector<std::string>& ex); // format previews (boot)
const std::vector<std::string>& name_examples();

int sleep_secs(); void set_sleep(int);   // deep-sleep idle timeout; 0 = never
int hold_secs();  void set_hold(int);    // hold-to-record seconds
int scroll();     void set_scroll(int);  // marquee speed (1..scroll_max)
int scroll_max();
int name_fmt();   void set_name_fmt(int);// date-time name format index (0..name_count-1)
int name_count();
bool disk_mode(); void set_disk(bool);   // USB mass-storage "disk mode" toggle

} // namespace sapiens::fw::settings
