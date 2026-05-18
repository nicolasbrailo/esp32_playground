#include "provisioning/wifi.h"

#include "common/c3_zero_led.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "esp_check.h"
#include "esp_log.h"
static const char *TAG = "wifi-prov";

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"

#define AP_SSID_PREFIX "blawifi-setup-"
#define AP_CHANNEL 1
#define AP_MAX_CONN 4
#define AP_IP "192.168.4.1"
#define POST_BODY_MAX 2048
#define NVS_NS "wifi_prov"
#define NVS_KEY_CFG "cfg"

static on_provisioning_complete_t g_complete_cb;

// Populated by save_post_handler, consumed by shutdown_softap_after_delay after
// the AP is torn down — so the callback can switch to STA mode without racing.
static struct provisioning_config g_pending_cfg;

// Tracked so init failures and shutdown can tear resources down in reverse.
static esp_netif_t *g_ap_netif = NULL;
static httpd_handle_t g_httpd_server = NULL;
static int g_dns_sock = -1;
static bool g_wifi_inited = false;
static bool g_wifi_started = false;

static void stop_http_server(void);
static void stop_captive_dns(void);
static void stop_softap(void);

// --- Blinking light while provisioning mode is active --------------------------------------------------------

static atomic_bool g_provisioning_wifi_led_active = false;
static SemaphoreHandle_t g_provisioning_wifi_led_done = NULL; // signalled when task exits
static bool g_provisioning_wifi_led_running = false;          // true between successful xTaskCreate and join

static void provisioning_wifi_led(void *arg) {
  while (atomic_load_explicit(&g_provisioning_wifi_led_active, memory_order_relaxed)) {
    c3_zero_led_blink(/*n=*/1, /*on_ms=*/200, /*off_ms=*/0, /*r=*/10, /*g=*/10, /*b=*/10);
    if (!atomic_load_explicit(&g_provisioning_wifi_led_active, memory_order_relaxed)) break;
    c3_zero_led_blink(/*n=*/1, /*on_ms=*/100, /*off_ms=*/0, /*r=*/40, /*g=*/40, /*b=*/40);
  }
  xSemaphoreGive(g_provisioning_wifi_led_done);
  vTaskDelete(NULL);
}

static void provisioning_wifi_led_start(void) {
  atomic_store_explicit(&g_provisioning_wifi_led_active, true, memory_order_relaxed);
  if (xTaskCreate(provisioning_wifi_led, "provisioning_wifi_led", 1 * 1024, NULL, 3, NULL) == pdPASS) {
    g_provisioning_wifi_led_running = true;
  } else {
    ESP_LOGW(TAG, "Failed to start provisioning_wifi_led task");
    atomic_store_explicit(&g_provisioning_wifi_led_active, false, memory_order_relaxed);
  }
}

static void provisioning_wifi_led_stop(void) {
  if (!g_provisioning_wifi_led_running)
    return;
  atomic_store_explicit(&g_provisioning_wifi_led_active, false, memory_order_relaxed);
  xSemaphoreTake(g_provisioning_wifi_led_done, portMAX_DELAY);
  g_provisioning_wifi_led_running = false;
}

// --- NVS --------------------------------------------------------------------

static esp_err_t load_cfg(struct provisioning_config *out) {
  nvs_handle_t h;
  esp_err_t rc = nvs_open(NVS_NS, NVS_READONLY, &h);
  if (rc != ESP_OK)
    return rc;

  size_t sz = sizeof(*out);
  rc = nvs_get_blob(h, NVS_KEY_CFG, out, &sz);
  nvs_close(h);
  if (rc != ESP_OK)
    return rc;
  // Reject blobs whose size doesn't match the current struct shape — likely
  // an older firmware wrote a different layout.
  if (sz != sizeof(*out))
    return ESP_ERR_INVALID_SIZE;
  return ESP_OK;
}

static esp_err_t save_cfg(const struct provisioning_config *cfg) {
  nvs_handle_t h;
  ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "nvs_open failed");
  esp_err_t rc = nvs_set_blob(h, NVS_KEY_CFG, cfg, sizeof(*cfg));
  if (rc == ESP_OK)
    rc = nvs_commit(h);
  nvs_close(h);
  return rc;
}

// --- HTTP -------------------------------------------------------------------

// Files in wifi_provision_www/ are embedded via EMBED_FILES (CMakeLists.txt).
// The linker uses the basename only for the symbol, with '.' → '_', so "foo.html" -> "foo_html"
// Use the WWW_HANDLER macro to create a handler for a static file, eg WWW_HANDLER(foo_html, "text/html")

#define WWW_HANDLER(fname, type)                                                                                       \
  static esp_err_t fname##_get_handler(httpd_req_t *req) {                                                             \
    extern const uint8_t fname##_start[] asm("_binary_" #fname "_start");                                              \
    extern const uint8_t fname##_end[] asm("_binary_" #fname "_end");                                                  \
    httpd_resp_set_type(req, type);                                                                                    \
    return httpd_resp_send(req, (const char *)fname##_start, fname##_end - fname##_start);                             \
  }

static esp_err_t www_handler_reg(httpd_handle_t server, const char *uri, int method,
                                 esp_err_t (*handler)(httpd_req_t *)) {
  const httpd_uri_t cfg = {
      .uri = uri,
      .method = method,
      .handler = handler,
  };
  return httpd_register_uri_handler(server, &cfg);
}

WWW_HANDLER(index_html, "text/html");
WWW_HANDLER(done_html, "text/html");
WWW_HANDLER(favicon_ico, "image/x-icon");

// Stops the SoftAP after a short delay so that done.html and its favicon have
// time to be fetched and rendered on the client before connectivity drops.
static void shutdown_softap_after_delay(void) {
  // 2 seconds should be enough for the client to fetch done.html and the icon. If the client
  // doesn't make it in this window, the config will still be stored, but the experience will look broken (a
  // failed-to-load page will be shown)
  vTaskDelay(pdMS_TO_TICKS(2000));
  ESP_LOGI(TAG, "Provisioning complete, tearing down provisioning stack");
  stop_http_server();
  stop_captive_dns();
  stop_softap(); // full teardown: esp_wifi_stop + esp_wifi_deinit + destroy AP netif

  // Persist before firing the callback so a crash in the callback doesn't lose
  // the config — the next boot will read it back from NVS.
  esp_err_t rc = save_cfg(&g_pending_cfg);
  if (rc != ESP_OK) {
    ESP_LOGE(TAG, "Failed to persist provisioning to NVS: %s", esp_err_to_name(rc));
  }

  // Callback runs *after* teardown so it can safely re-init wifi for STA mode.
  if (g_complete_cb)
    g_complete_cb(&g_pending_cfg);
}

static void shutdown_softap_task(void *arg) {
  shutdown_softap_after_delay();
  vTaskDelete(NULL);
}

// Decode application/x-www-form-urlencoded value: '+' → space, %xx → byte.
static void url_decode(char *dst, const char *src, size_t dst_sz) {
  size_t i = 0;
  while (*src && i + 1 < dst_sz) {
    if (*src == '+') {
      dst[i++] = ' ';
      src++;
    } else if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
      char hex[3] = {src[1], src[2], 0};
      dst[i++] = (char)strtol(hex, NULL, 16);
      src += 3;
    } else {
      dst[i++] = *src++;
    }
  }
  dst[i] = 0;
}

static void extract_field(const char *body, const char *key, char *dst, size_t dst_sz) {
  char enc[256];
  if (httpd_query_key_value(body, key, enc, sizeof(enc)) == ESP_OK) {
    url_decode(dst, enc, dst_sz);
  } else {
    dst[0] = 0;
  }
}

static esp_err_t save_post_handler(httpd_req_t *req) {
  if (req->content_len <= 0 || req->content_len >= POST_BODY_MAX) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad content length");
  }

  char *body = malloc(req->content_len + 1);
  if (!body) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
  }

  int total = 0;
  while (total < req->content_len) {
    int r = httpd_req_recv(req, body + total, req->content_len - total);
    if (r <= 0) {
      if (r == HTTPD_SOCK_ERR_TIMEOUT)
        continue;
      free(body);
      return ESP_FAIL;
    }
    total += r;
  }
  body[total] = 0;

  memset(&g_pending_cfg, 0, sizeof(g_pending_cfg));
  extract_field(body, "this_device_name", g_pending_cfg.this_device_name, sizeof(g_pending_cfg.this_device_name));
  extract_field(body, "ap_name", g_pending_cfg.ap_name, sizeof(g_pending_cfg.ap_name));
  extract_field(body, "ap_pwd", g_pending_cfg.ap_pwd, sizeof(g_pending_cfg.ap_pwd));
  extract_field(body, "mqtt_url", g_pending_cfg.mqtt_url, sizeof(g_pending_cfg.mqtt_url));
  extract_field(body, "mqtt_usr", g_pending_cfg.mqtt_usr, sizeof(g_pending_cfg.mqtt_usr));
  extract_field(body, "mqtt_pwd", g_pending_cfg.mqtt_pwd, sizeof(g_pending_cfg.mqtt_pwd));
  free(body);

  // Schedule SoftAP teardown + completion callback. The callback fires after
  // the AP is down so the caller can take over the radio for STA mode.
  bool deferred = (xTaskCreate(shutdown_softap_task, "shutdown_ap", 4 * 1024, NULL, 5, NULL) == pdPASS);
  if (!deferred) {
    ESP_LOGW(TAG,
             "xTaskCreate(shutdown_ap) failed; will tear down SoftAP and run callback synchronously after response");
  }

  // 303 See Other is the right code for a POST → GET redirect.
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", "http://" AP_IP "/done.html");
  esp_err_t rc = httpd_resp_send(req, NULL, 0);

  // Fallback path: blocks this handler thread until the AP is down. The client
  // likely won't get done.html, but at least the device can move on to STA mode.
  if (!deferred) {
    shutdown_softap_after_delay();
  }
  return rc;
}

// Catches anything not explicitly handled (i.e. anything but "/").
// Returns a 302 → http://AP_IP/ so the OS captive-portal detector kicks in.
static esp_err_t redirect_to_root(httpd_req_t *req, httpd_err_code_t err) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://" AP_IP "/");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static void stop_http_server(void) {
  if (g_httpd_server) {
    httpd_stop(g_httpd_server);
    g_httpd_server = NULL;
  }
}

static esp_err_t start_http_server(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  esp_err_t ret;
  ESP_RETURN_ON_ERROR(httpd_start(&g_httpd_server, &config), TAG, "httpd_start failed");
  ESP_GOTO_ON_ERROR(www_handler_reg(g_httpd_server, "/", HTTP_GET, index_html_get_handler), fail, TAG,
                    "register / failed");
  ESP_GOTO_ON_ERROR(www_handler_reg(g_httpd_server, "/done.html", HTTP_GET, done_html_get_handler), fail, TAG,
                    "register /done.html failed");
  ESP_GOTO_ON_ERROR(www_handler_reg(g_httpd_server, "/favicon.ico", HTTP_GET, favicon_ico_get_handler), fail, TAG,
                    "register /favicon.ico failed");
  ESP_GOTO_ON_ERROR(www_handler_reg(g_httpd_server, "/save", HTTP_POST, save_post_handler), fail, TAG,
                    "register /save failed");
  ESP_GOTO_ON_ERROR(httpd_register_err_handler(g_httpd_server, HTTPD_404_NOT_FOUND, redirect_to_root), fail, TAG,
                    "register 404 handler failed");
  return ESP_OK;
fail:
  stop_http_server();
  return ret;
}

// --- DNS --------------------------------------------------------------------

// Minimal DNS server: parses the question, then appends a single A-record
// answer pointing at AP_IP. Works for any QNAME, which is the point.
static void captive_dns_task(void *arg) {
  g_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (g_dns_sock < 0) {
    ESP_LOGE(TAG, "DNS socket() failed: errno %d", errno);
    vTaskDelete(NULL);
  }

  struct sockaddr_in saddr = {
      .sin_family = AF_INET,
      .sin_port = htons(53),
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  if (bind(g_dns_sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
    ESP_LOGE(TAG, "DNS bind() failed: errno %d", errno);
    close(g_dns_sock);
    g_dns_sock = -1;
    vTaskDelete(NULL);
  }

  const uint32_t answer_ip = inet_addr(AP_IP); // already network-order

  uint8_t buf[512];
  for (;;) {
    struct sockaddr_in caddr;
    socklen_t caddr_len = sizeof(caddr);
    int n = recvfrom(g_dns_sock, buf, sizeof(buf), 0, (struct sockaddr *)&caddr, &caddr_len);
    if (n < 0)
      break; // socket closed by stop_captive_dns
    if (n < 12)
      continue; // smaller than DNS header

    // Walk QNAME (length-prefixed labels, terminated by a zero byte).
    int p = 12;
    while (p < n && buf[p] != 0) {
      p += buf[p] + 1;
    }
    if (p >= n)
      continue;
    p += 1 + 4; // null label + QTYPE(2) + QCLASS(2)
    if (p > n)
      continue;

    // Flip flags to response: QR=1, AA=1, preserve RD. RA=0, RCODE=0.
    buf[2] |= 0x84;
    buf[3] = 0x00;
    // ANCOUNT = 1, NSCOUNT = ARCOUNT = 0.
    buf[6] = 0x00;
    buf[7] = 0x01;
    buf[8] = 0x00;
    buf[9] = 0x00;
    buf[10] = 0x00;
    buf[11] = 0x00;

    // Append answer: NAME = pointer to QNAME at offset 12, type A, class IN, TTL=60s, RDLENGTH=4.
    if (p + 16 > (int)sizeof(buf))
      continue;
    buf[p++] = 0xC0;
    buf[p++] = 0x0C;
    buf[p++] = 0x00;
    buf[p++] = 0x01;
    buf[p++] = 0x00;
    buf[p++] = 0x01;
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    buf[p++] = 0x3C;
    buf[p++] = 0x00;
    buf[p++] = 0x04;
    memcpy(&buf[p], &answer_ip, 4);
    p += 4;

    sendto(g_dns_sock, buf, p, 0, (struct sockaddr *)&caddr, caddr_len);
  }

  close(g_dns_sock);
  g_dns_sock = -1;
  vTaskDelete(NULL);
}

static void stop_captive_dns(void) {
  // shutdown() wakes recvfrom with n<0; the task then closes the fd and exits.
  if (g_dns_sock >= 0) {
    shutdown(g_dns_sock, SHUT_RDWR);
  }
}

// --- SoftAP -----------------------------------------------------------------

// Tell the DHCP server to advertise the AP itself as the DNS server, so client
// devices send their lookups our way (where captive_dns_task hijacks them).
static esp_err_t configure_dhcp_dns(esp_netif_t *ap_netif) {
  ESP_RETURN_ON_ERROR(esp_netif_dhcps_stop(ap_netif), TAG, "dhcps_stop failed");

  esp_netif_dns_info_t dns_info = {
      .ip = {.type = ESP_IPADDR_TYPE_V4},
  };
  dns_info.ip.u_addr.ip4.addr = inet_addr(AP_IP);
  ESP_RETURN_ON_ERROR(esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info), TAG, "set_dns_info failed");

  uint8_t offer_dns = 1;
  ESP_RETURN_ON_ERROR(
      esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns)),
      TAG, "dhcps_option failed");

  ESP_RETURN_ON_ERROR(esp_netif_dhcps_start(ap_netif), TAG, "dhcps_start failed");
  return ESP_OK;
}

// Unwinds whatever start_softap managed to set up. Does not touch
// esp_netif_init / esp_event_loop_create_default — those are process-wide
// shared infra and may be in use by other components.
static void stop_softap(void) {
  if (g_wifi_started) {
    esp_wifi_stop();
    g_wifi_started = false;
  }
  if (g_wifi_inited) {
    esp_wifi_deinit();
    g_wifi_inited = false;
  }
  if (g_ap_netif) {
    esp_netif_destroy_default_wifi(g_ap_netif);
    g_ap_netif = NULL;
  }

  provisioning_wifi_led_stop();
}

static esp_err_t start_softap(void) {
  esp_err_t ret;
  // Shared infra — not unwound on failure.
  ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
  ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event_loop_create_default failed");

  g_ap_netif = esp_netif_create_default_wifi_ap();
  if (!g_ap_netif) {
    ESP_LOGE(TAG, "esp_netif_create_default_wifi_ap failed");
    return ESP_FAIL;
  }

  wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_GOTO_ON_ERROR(esp_wifi_init(&wifi_cfg), fail, TAG, "esp_wifi_init failed");
  g_wifi_inited = true;

  // Build a per-device SSID by appending the last 2 bytes of the SoftAP MAC.
  // MAC-derived (not esp_random()) so the SSID stays stable across reboots.
  uint8_t mac[6];
  ESP_GOTO_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), fail, TAG, "esp_read_mac failed");
  char ssid[32];
  int ssid_len = snprintf(ssid, sizeof(ssid), AP_SSID_PREFIX "%02X%02X", mac[4], mac[5]);

  wifi_config_t ap_cfg = {
      .ap =
          {
              .ssid_len = ssid_len,
              .channel = AP_CHANNEL,
              .max_connection = AP_MAX_CONN,
              .authmode = WIFI_AUTH_OPEN,
          },
  };
  memcpy(ap_cfg.ap.ssid, ssid, ssid_len);

  ESP_GOTO_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), fail, TAG, "esp_wifi_set_mode failed");
  ESP_GOTO_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg), fail, TAG, "esp_wifi_set_config failed");
  ESP_GOTO_ON_ERROR(esp_wifi_start(), fail, TAG, "esp_wifi_start failed");
  g_wifi_started = true;

  ESP_GOTO_ON_ERROR(configure_dhcp_dns(g_ap_netif), fail, TAG, "configure_dhcp_dns failed");

  ESP_LOGI(TAG, "SoftAP started. SSID=\"%s\" (open). Captive portal at http://%s/", ssid, AP_IP);
  provisioning_wifi_led_start();
  return ESP_OK;
fail:
  stop_softap();
  return ret;
}

// --- Public entry -----------------------------------------------------------

esp_err_t wifi_provision_init(on_provisioning_complete_t cb) {
  g_complete_cb = cb;
  atomic_init(&g_provisioning_wifi_led_active, false);
  g_provisioning_wifi_led_done = xSemaphoreCreateBinary();

  // If a previous provisioning is persisted, skip the captive portal entirely
  // and hand the cached config straight to the caller.
  esp_err_t load_rc = load_cfg(&g_pending_cfg);
  if (load_rc == ESP_OK) {
    ESP_LOGI(TAG, "Loaded provisioning from NVS; skipping captive portal");
    if (g_complete_cb)
      g_complete_cb(&g_pending_cfg);
    return ESP_OK;
  }
  if (load_rc != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "NVS load failed (%s); falling back to provisioning UI", esp_err_to_name(load_rc));
  }

  ESP_RETURN_ON_ERROR(start_softap(), TAG, "start_softap failed");

  esp_err_t rc = start_http_server();
  if (rc != ESP_OK) {
    ESP_LOGE(TAG, "start_http_server failed: %s", esp_err_to_name(rc));
    stop_softap();
    return rc;
  }

  if (xTaskCreate(captive_dns_task, "captive_dns", 4 * 1024, NULL, 5, NULL) != pdPASS) {
    ESP_LOGE(TAG, "xTaskCreate(captive_dns) failed");
    stop_http_server();
    stop_softap();
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
