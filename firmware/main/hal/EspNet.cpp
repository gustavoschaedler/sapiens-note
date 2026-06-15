#include "EspNet.h"

#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "Logo.h" // webtool: sapiens::web::kLogoDataUri

namespace sapiens::fw {
namespace {
const char* TAG = "sapiens.net";
constexpr int BIT_GOT_IP = BIT0;
constexpr int BIT_FAIL = BIT1;
constexpr char NVS_NS[] = "wifi";

EventGroupHandle_t s_events = nullptr;
esp_netif_t* s_sta_netif = nullptr;
esp_netif_t* s_ap_netif = nullptr;
bool s_inited = false;
bool s_started = false;
int s_retries = 0;
volatile bool s_provisioned = false;
httpd_handle_t s_httpd = nullptr;
TaskHandle_t s_dns_task = nullptr;
volatile bool s_dns_run = false;
char s_ap_ssid[20] = "SapiensNote";
std::vector<std::string> s_scanned; // SSIDs from the last scan (cached for the page)

// ---- NVS credential helpers -------------------------------------------------
bool nvs_get(const char* key, char* out, size_t cap) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = cap;
    esp_err_t e = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    return e == ESP_OK && out[0];
}
void nvs_put(const char* ssid, const char* pass) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass);
    nvs_commit(h);
    nvs_close(h);
}

// ---- WiFi/IP events ---------------------------------------------------------
void on_event(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retries < 8) {
            ++s_retries;
            esp_wifi_connect();
        } else if (s_events) {
            xEventGroupSetBits(s_events, BIT_FAIL);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retries = 0;
        if (s_events) xEventGroupSetBits(s_events, BIT_GOT_IP);
    }
}

// ---- tiny urldecode for the POST body --------------------------------------
void urldecode(const char* in, char* out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < cap; ++i) {
        if (in[i] == '%' && in[i + 1] && in[i + 2]) {
            auto hex = [](char c) {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            out[o++] = (char)(hex(in[i + 1]) * 16 + hex(in[i + 2]));
            i += 2;
        } else if (in[i] == '+') {
            out[o++] = ' ';
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = 0;
}
void form_field(const char* body, const char* key, char* out, size_t cap) {
    out[0] = 0;
    char pat[24];
    std::snprintf(pat, sizeof(pat), "%s=", key);
    const char* p = std::strstr(body, pat);
    if (!p) return;
    p += std::strlen(pat);
    const char* end = std::strchr(p, '&');
    size_t n = end ? (size_t)(end - p) : std::strlen(p);
    char raw[128];
    if (n >= sizeof(raw)) n = sizeof(raw) - 1;
    std::memcpy(raw, p, n);
    raw[n] = 0;
    urldecode(raw, out, cap);
}

// ---- captive DNS: answer every A query with the AP IP (192.168.4.1) ---------
void dns_task(void*) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { s_dns_task = nullptr; vTaskDelete(nullptr); return; }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(sock, (sockaddr*)&addr, sizeof(addr));
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    uint8_t buf[512];
    while (s_dns_run) {
        sockaddr_in src = {};
        socklen_t sl = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&src, &sl);
        if (n < (int)sizeof(uint16_t) * 6) continue;
        buf[2] |= 0x80;            // QR = response
        buf[3] = 0x80;             // RA
        buf[6] = 0; buf[7] = 1;    // ANCOUNT = 1
        // Append answer: name ptr 0xC00C, type A, class IN, TTL 60, len 4, IP.
        int len = n;
        uint8_t ans[] = { 0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
                          0x3C, 0x00, 0x04, 192, 168, 4, 1 };
        if (len + (int)sizeof(ans) <= (int)sizeof(buf)) {
            std::memcpy(buf + len, ans, sizeof(ans));
            len += sizeof(ans);
            sendto(sock, buf, len, 0, (sockaddr*)&src, sl);
        }
    }
    close(sock);
    s_dns_task = nullptr;
    vTaskDelete(nullptr);
}

// ---- portal HTTP handlers ---------------------------------------------------
// Escape a string for use inside an HTML text node / attribute / JS single-quote.
std::string esc(const std::string& in) {
    std::string o;
    for (char c : in) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "\\'"; break;
            default: o += c;
        }
    }
    return o;
}

esp_err_t h_root(httpd_req_t* req) {
    ESP_LOGI(TAG, "portal GET %s", req->uri);
    std::string p =
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Sapiens Note WiFi</title>"
        "<style>body{font-family:system-ui;margin:0;background:#0e0f12;color:#f0f0f0}"
        ".w{max-width:440px;margin:0 auto;padding:22px}h1{font-size:22px;margin:.2em 0}"
        "label{display:block;margin:14px 0 4px;font-size:14px;opacity:.8}"
        "input,button{width:100%;box-sizing:border-box;padding:14px;border-radius:10px;"
        "border:1px solid #333;background:#191b1f;color:#fff;font-size:17px}"
        "button{background:#22a06b;border:0;font-weight:700;margin-top:16px}"
        ".nets{border:1px solid #2a2d33;border-radius:10px;overflow:hidden;margin-top:6px}"
        ".net{padding:13px 14px;border-top:1px solid #23262b;cursor:pointer;font-size:16px}"
        ".net:first-child{border-top:0}.net:active{background:#22a06b}"
        ".hint{opacity:.55;font-size:13px;margin-top:10px}a{color:#5cd6a0}"
        ".hd{display:flex;align-items:center;gap:12px;margin-bottom:4px}"
        ".hd img{width:44px;height:44px}.hd h1{font-size:22px;margin:0}</style>"
        "<div class=w><div class=hd><img src='";
    p += sapiens::web::kLogoDataUri;
    p += "' alt='Sapiens Note'><h1>Sapiens Note &middot; Wi-Fi</h1></div>";
    p +=
        "<form method=POST action=/save>"
        "<label>Network (tap one below or type)</label>"
        "<input name=ssid id=ssid autocomplete=off placeholder='network name'>";
    if (!s_scanned.empty()) {
        p += "<div class=nets>";
        for (const auto& s : s_scanned) {
            std::string e = esc(s);
            p += "<div class=net onclick=\"document.getElementById('ssid').value='" + e +
                 "'\">" + e + "</div>";
        }
        p += "</div>";
    } else {
        p += "<p class=hint>No networks found &mdash; type the name manually. "
             "<a href=/>rescan</a></p>";
    }
    p += "<label>Password</label>"
         "<input name=pass type=password autocomplete=off placeholder='password'>"
         "<button type=submit>Connect</button></form>"
         "<p class=hint>The device will save and connect to this network.</p></div>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, p.c_str(), p.size());
    return ESP_OK;
}

esp_err_t h_save(httpd_req_t* req) {
    char body[300] = {0};
    int total = req->content_len < (int)sizeof(body) - 1 ? req->content_len
                                                         : (int)sizeof(body) - 1;
    int r = 0;
    while (r < total) {
        int k = httpd_req_recv(req, body + r, total - r);
        if (k <= 0) break;
        r += k;
    }
    body[r] = 0;
    char ssid[64] = {0}, pass[96] = {0};
    form_field(body, "ssid", ssid, sizeof(ssid));
    form_field(body, "pass", pass, sizeof(pass));
    if (ssid[0]) {
        nvs_put(ssid, pass);
        s_provisioned = true;
        ESP_LOGI(TAG, "portal saved SSID '%s'", ssid);
    }
    const char* ok =
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:system-ui;background:#0e0f12;color:#f0f0f0;text-align:center;padding:40px}</style>"
        "<h2>Saved!</h2><p>Sapiens Note will connect to this network.<br>You can close this page.</p>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ok, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Make the AP advertise itself (192.168.4.1) as the DNS server, so a client's
// captive-portal probe resolves through our DNS hijack and the page auto-opens.
void configure_ap_captive_dns() {
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_dns_info_t dns = {};
    dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("192.168.4.1");
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    uint8_t offer_dns = 2; // DHCPS_OFFER_DNS
    esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                           &offer_dns, sizeof(offer_dns));
    esp_netif_dhcps_start(s_ap_netif);
}

void start_httpd_portal() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) return;
    httpd_uri_t save = { "/save", HTTP_POST, h_save, nullptr };
    httpd_register_uri_handler(s_httpd, &save);
    httpd_uri_t root = { "/*", HTTP_GET, h_root, nullptr }; // any path -> portal (captive)
    httpd_register_uri_handler(s_httpd, &root);
}
} // namespace

void EspNet::init() {
    if (s_inited) return;
    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr, nullptr);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    std::snprintf(s_ap_ssid, sizeof(s_ap_ssid), "SapiensNote-%02X%02X", mac[4], mac[5]);
    s_inited = true;
}

bool EspNet::has_credentials() {
    char ssid[64] = {0};
    return nvs_get("ssid", ssid, sizeof(ssid));
}

void EspNet::start_portal() {
    s_provisioned = false;
    esp_wifi_set_mode(WIFI_MODE_APSTA); // AP for the portal, STA so we can scan
    wifi_config_t ap = {};
    std::strncpy((char*)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = std::strlen(s_ap_ssid);
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.channel = 1;
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (!s_started) { esp_wifi_start(); s_started = true; }
    vTaskDelay(pdMS_TO_TICKS(100));
    configure_ap_captive_dns();         // advertise our DNS -> captive auto-open
    esp_wifi_scan_start(nullptr, true); // blocking scan for the portal list
    s_scanned.clear();
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 24) n = 24;
    if (n) {
        std::vector<wifi_ap_record_t> recs(n);
        if (esp_wifi_scan_get_ap_records(&n, recs.data()) == ESP_OK) {
            for (int i = 0; i < n; ++i) {
                const char* s = (const char*)recs[i].ssid;
                if (!s[0]) continue;
                bool dup = false;
                for (auto& e : s_scanned) if (e == s) { dup = true; break; }
                if (!dup) s_scanned.emplace_back(s);
            }
        }
    }
    ESP_LOGI(TAG, "scan found %d network(s)", (int)s_scanned.size());
    s_dns_run = true;
    xTaskCreate(dns_task, "sn_dns", 4096, nullptr, 5, &s_dns_task);
    start_httpd_portal();
    ESP_LOGI(TAG, "captive portal up: AP '%s' @ 192.168.4.1", s_ap_ssid);
}

void EspNet::stop_portal() {
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = nullptr; }
    s_dns_run = false;
    for (int i = 0; i < 20 && s_dns_task; ++i) vTaskDelay(pdMS_TO_TICKS(50));
}

bool EspNet::provisioned() const { return s_provisioned; }

std::string EspNet::ap_info() const {
    return std::string(s_ap_ssid) + " / 192.168.4.1";
}

std::string EspNet::ap_ssid() const { return s_ap_ssid; }

bool EspNet::connect(std::uint32_t timeout_ms) {
    char ssid[64] = {0}, pass[96] = {0};
    if (!nvs_get("ssid", ssid, sizeof(ssid))) return false;
    nvs_get("pass", pass, sizeof(pass));

    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t sta = {};
    std::strncpy((char*)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    std::strncpy((char*)sta.sta.password, pass, sizeof(sta.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (!s_started) { esp_wifi_start(); s_started = true; }
    s_retries = 0;
    xEventGroupClearBits(s_events, BIT_GOT_IP | BIT_FAIL);
    esp_wifi_connect();
    EventBits_t b = xEventGroupWaitBits(s_events, BIT_GOT_IP | BIT_FAIL, pdFALSE,
                                        pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return (b & BIT_GOT_IP) != 0;
}

void EspNet::disconnect() {
    esp_wifi_disconnect();
    if (s_started) { esp_wifi_stop(); s_started = false; }
}

bool EspNet::connected() const {
    if (!s_events) return false;
    return (xEventGroupGetBits(s_events) & BIT_GOT_IP) != 0;
}

std::string EspNet::ip() const {
    esp_netif_ip_info_t info = {};
    if (s_sta_netif && esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK && info.ip.addr) {
        char b[16];
        std::snprintf(b, sizeof(b), IPSTR, IP2STR(&info.ip));
        return b;
    }
    return "";
}

bool EspNet::sync_ntp(std::uint32_t timeout_ms) {
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&cfg);
    bool ok = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
    esp_netif_sntp_deinit();
    if (ok) {
        time_t now = ::time(nullptr);
        ok = now > 1700000000; // sanity: after 2023
    }
    ESP_LOGI(TAG, "NTP sync %s", ok ? "ok" : "failed");
    return ok;
}

} // namespace sapiens::fw
