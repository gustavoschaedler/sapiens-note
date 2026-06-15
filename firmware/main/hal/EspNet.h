#pragma once
// WiFi networking for the device: captive-portal provisioning (SoftAP), STA
// connect using credentials stored in NVS, and SNTP time sync. The on-demand web
// dashboard (Phase B) layers an HTTP server on top once connected.
#include <cstdint>
#include <string>

namespace sapiens::fw {

class EspNet {
public:
    void init(); // one-time: netif + default event loop + wifi init (idle)

    bool has_credentials();      // NVS holds a saved SSID

    // Captive-portal provisioning: starts a SoftAP + a config page + DNS hijack so
    // a phone auto-opens it. Non-blocking; poll provisioned() until the user saves
    // credentials (written to NVS). stop_portal() tears it down.
    void start_portal();
    void stop_portal();
    bool provisioned() const;
    std::string ap_info() const; // "SapiensNote-XXXX / 192.168.4.1"
    std::string ap_ssid() const; // just the AP SSID

    // Station mode.
    bool connect(std::uint32_t timeout_ms = 15000); // uses stored creds
    void disconnect();
    bool connected() const;
    std::string ip() const;      // STA IP ("" if not connected)

    // SNTP time sync (system clock); returns true once time is set.
    bool sync_ntp(std::uint32_t timeout_ms = 10000);
};

} // namespace sapiens::fw
