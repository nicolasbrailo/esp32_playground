#include "wifi.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

struct wifi_cbs g_cbs;
static const char *TAG = "blawifi";

// Cached SSID for log messages; the driver keeps its own copy of the config.
static char g_ssid[32];

// Human-readable name for wifi_err_reason_t. ESP-IDF doesn't ship a built-in
// converter; only the most common codes are spelled out, the rest fall through
// to a hex print.
static const char *wifi_reason_str(uint8_t reason) {
  switch (reason) {
  case WIFI_REASON_UNSPECIFIED:
    return "UNSPECIFIED";
  case WIFI_REASON_AUTH_EXPIRE:
    return "AUTH_EXPIRE";
  case WIFI_REASON_AUTH_LEAVE:
    return "AUTH_LEAVE";
  case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY:
    return "DISASSOC_DUE_TO_INACTIVITY";
  case WIFI_REASON_ASSOC_TOOMANY:
    return "ASSOC_TOOMANY";
  case WIFI_REASON_CLASS2_FRAME_FROM_NONAUTH_STA:
    return "CLASS2_FRAME_FROM_NONAUTH_STA";
  case WIFI_REASON_CLASS3_FRAME_FROM_NONASSOC_STA:
    return "CLASS3_FRAME_FROM_NONASSOC_STA";
  case WIFI_REASON_ASSOC_LEAVE:
    return "ASSOC_LEAVE";
  case WIFI_REASON_ASSOC_NOT_AUTHED:
    return "ASSOC_NOT_AUTHED";
  case WIFI_REASON_MIC_FAILURE:
    return "MIC_FAILURE";
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    return "4WAY_HANDSHAKE_TIMEOUT (wrong password?)";
  case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
    return "GROUP_KEY_UPDATE_TIMEOUT";
  case WIFI_REASON_IE_IN_4WAY_DIFFERS:
    return "IE_IN_4WAY_DIFFERS";
  case WIFI_REASON_GROUP_CIPHER_INVALID:
    return "GROUP_CIPHER_INVALID";
  case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
    return "PAIRWISE_CIPHER_INVALID";
  case WIFI_REASON_AKMP_INVALID:
    return "AKMP_INVALID";
  case WIFI_REASON_CIPHER_SUITE_REJECTED:
    return "CIPHER_SUITE_REJECTED";
  case WIFI_REASON_BEACON_TIMEOUT:
    return "BEACON_TIMEOUT (AP out of range or off)";
  case WIFI_REASON_NO_AP_FOUND:
    return "NO_AP_FOUND (SSID not seen in scan)";
  case WIFI_REASON_AUTH_FAIL:
    return "AUTH_FAIL (wrong password?)";
  case WIFI_REASON_ASSOC_FAIL:
    return "ASSOC_FAIL";
  case WIFI_REASON_HANDSHAKE_TIMEOUT:
    return "HANDSHAKE_TIMEOUT";
  case WIFI_REASON_CONNECTION_FAIL:
    return "CONNECTION_FAIL";
  case WIFI_REASON_AP_TSF_RESET:
    return "AP_TSF_RESET";
  case WIFI_REASON_ROAMING:
    return "ROAMING";
  default:
    return "UNKNOWN";
  }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "STA started, connecting to \"%s\"", g_ssid);
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t *e = data;
    ESP_LOGW(TAG, "Disconnected from \"%s\" (reason=%d %s); retrying", g_ssid, e->reason, wifi_reason_str(e->reason));
    // Scan/auth latency in esp_wifi_connect provides a natural debounce, so no
    // explicit backoff is needed here.
    esp_wifi_connect();
    g_cbs.on_wifi_down();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *e = data;
    g_cbs.on_wifi_up(e->ip_info.ip);
    ESP_LOGI(TAG, "Wifi connected, got IP: " IPSTR, IP2STR(&e->ip_info.ip));
  }
}

esp_err_t wifi_connect(const char *ap_name, const char *ap_pwd, struct wifi_cbs cbs) {
  g_cbs = cbs;
  strlcpy(g_ssid, ap_name, sizeof(g_ssid));

  // esp_netif_init / esp_event_loop_create_default may already have been
  // called (e.g. by the captive-portal path). Treat INVALID_STATE as "already
  // initialized" rather than an error.
  esp_err_t rc = esp_netif_init();
  if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(rc));
    return rc;
  }
  rc = esp_event_loop_create_default();
  if (rc != ESP_OK && rc != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "event_loop_create_default failed: %s", esp_err_to_name(rc));
    return rc;
  }

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_cfg), TAG, "esp_wifi_init failed");

  ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL), TAG,
                      "WIFI_EVENT handler register failed");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL), TAG,
                      "IP_EVENT handler register failed");

  wifi_config_t sta_cfg = {0};
  strlcpy((char *)sta_cfg.sta.ssid, ap_name, sizeof(sta_cfg.sta.ssid));
  strlcpy((char *)sta_cfg.sta.password, ap_pwd, sizeof(sta_cfg.sta.password));
  // Sweep all channels (vs. fast-scan defaulting to the last-known channel)
  // and pick the strongest BSSID — avoids wrong-channel retries on cold boot.
  sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg), TAG, "esp_wifi_set_config failed");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");
  return ESP_OK;
}
