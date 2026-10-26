#pragma once
#include "esp_wifi_types.h"
// Minimal esp_wifi.h stub. The .cpp files that call promiscuous / raw WiFi APIs
// are excluded via build_src_filter; this header exists only so apps that
// include <esp_wifi.h> for types (WIFI_AUTH_*, wifi_mode_t) compile on the
// simulator without modification.

typedef enum {
    WIFI_MODE_NULL  = 0,
    WIFI_MODE_STA   = 1,
    WIFI_MODE_AP    = 2,
    WIFI_MODE_APSTA = 3,
    WIFI_MODE_MAX
} wifi_mode_t;

typedef enum {
    WIFI_SECOND_CHAN_NONE = 0,
    WIFI_SECOND_CHAN_ABOVE,
    WIFI_SECOND_CHAN_BELOW,
} wifi_second_chan_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    uint8_t ssid_len;
    uint8_t channel;
    uint8_t authmode;
    uint8_t ssid_hidden;
    uint8_t max_connection;
    uint8_t beacon_interval;
} wifi_ap_config_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    bool scan_method;
    bool bssid_set;
    uint8_t bssid[6];
    uint8_t channel;
    uint32_t listen_interval;
    uint8_t sort_method;
    uint8_t threshold_rssi;
    uint8_t threshold_authmode;
    bool pmf_cfg_capable;
    bool pmf_cfg_required;
    uint32_t rm_enabled;
    uint32_t btm_enabled;
    uint32_t mbo_enabled;
    uint32_t ft_enabled;
    uint32_t owe_enabled;
    uint32_t transition_disable;
    uint32_t reserved;
    uint8_t sae_pwe_h2e;
    uint8_t failure_retry_cnt;
} wifi_sta_config_t;

typedef union {
    wifi_ap_config_t  ap;
    wifi_sta_config_t sta;
} wifi_config_t;

inline int esp_wifi_set_mode(wifi_mode_t) { return 0; }
inline int esp_wifi_get_mode(wifi_mode_t* mode) { if (mode) *mode = WIFI_MODE_NULL; return 0; }
inline int esp_wifi_set_channel(uint8_t, wifi_second_chan_t) { return 0; }
inline int esp_wifi_set_promiscuous(bool) { return 0; }
inline int esp_wifi_set_promiscuous_filter(const void*) { return 0; }
inline int esp_wifi_set_promiscuous_rx_cb(void*) { return 0; }
inline int esp_wifi_set_config(int, wifi_config_t*) { return 0; }
inline int esp_wifi_set_mac(int, const uint8_t*) { return 0; }
