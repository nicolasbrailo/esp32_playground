#include "common/btn_mon.h"
#include "common/c3_zero_led.h"
#include "provisioning/reset.h"
#include "provisioning/wifi.h"
#include "wifi.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#define TAG "blawifi"

// In ESP32 C3 zero, the boot button is wired to GPIO 9
#define BTN_GPIO 9
#define UPTIME_PUBLISH_INTERVAL_MS 5000

// Populated by on_provisioning_complete; read by mqtt_app_start and the
// uptime task. Single-writer, multi-reader after provisioning completes.
static struct provisioning_config g_cfg;
static bool g_cfg_ready = false;

// MQTT lifecycle: handle + connected flag, both guarded by g_mqtt_mutex.
// g_mqtt_connected is also read lock-free by the uptime task; atomic_bool
// makes that safe.
static esp_mqtt_client_handle_t g_mqtt_client = NULL;
static atomic_bool g_mqtt_connected = false;
static SemaphoreHandle_t g_mqtt_mutex = NULL;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  (void)handler_args;
  (void)base;
  esp_mqtt_event_handle_t event = event_data;
  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
    atomic_store(&g_mqtt_connected, true);
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    atomic_store(&g_mqtt_connected, false);
    break;
  case MQTT_EVENT_ERROR:
    ESP_LOGW(TAG, "MQTT_EVENT_ERROR (type=0x%x)", event->error_handle->error_type);
    atomic_store(&g_mqtt_connected, false);
    break;
  default:
    break;
  }
}

static void mqtt_app_start(void) {
  if (!g_cfg_ready) {
    ESP_LOGW(TAG, "mqtt_app_start called before provisioning; skipping");
    return;
  }
  xSemaphoreTake(g_mqtt_mutex, portMAX_DELAY);
  if (g_mqtt_client != NULL) {
    xSemaphoreGive(g_mqtt_mutex);
    return; // already running
  }
  const esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = g_cfg.mqtt_url,
      .credentials.username = g_cfg.mqtt_usr,
      .credentials.authentication.password = g_cfg.mqtt_pwd,
  };
  g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  if (g_mqtt_client == NULL) {
    ESP_LOGE(TAG, "esp_mqtt_client_init failed");
    xSemaphoreGive(g_mqtt_mutex);
    return;
  }
  esp_mqtt_client_register_event(g_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
  esp_err_t rc = esp_mqtt_client_start(g_mqtt_client);
  if (rc != ESP_OK) {
    ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(rc));
    esp_mqtt_client_destroy(g_mqtt_client);
    g_mqtt_client = NULL;
  }
  xSemaphoreGive(g_mqtt_mutex);
}

static void mqtt_app_stop(void) {
  xSemaphoreTake(g_mqtt_mutex, portMAX_DELAY);
  atomic_store(&g_mqtt_connected, false);
  if (g_mqtt_client != NULL) {
    // Best-effort local teardown — the broker won't see a clean DISCONNECT
    // because the underlying TCP socket is already dead.
    esp_mqtt_client_stop(g_mqtt_client);
    esp_mqtt_client_destroy(g_mqtt_client);
    g_mqtt_client = NULL;
  }
  xSemaphoreGive(g_mqtt_mutex);
}

static void uptime_task(void *arg) {
  (void)arg;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(UPTIME_PUBLISH_INTERVAL_MS));
    if (!atomic_load(&g_mqtt_connected))
      continue;

    xSemaphoreTake(g_mqtt_mutex, portMAX_DELAY);
    if (g_mqtt_client != NULL && atomic_load(&g_mqtt_connected)) {
      char topic[96];
      snprintf(topic, sizeof(topic), "%s/uptime", g_cfg.this_device_name);
      char payload[24];
      int64_t s = esp_timer_get_time() / 1000000;
      int n = snprintf(payload, sizeof(payload), "%" PRId64, s);
      esp_mqtt_client_publish(g_mqtt_client, topic, payload, n, 0, 0);
    }
    xSemaphoreGive(g_mqtt_mutex);
  }
}

static void on_wifi_up(esp_ip4_addr_t ip) {
  (void)ip;
  ESP_LOGI(TAG, "CONN UP");
  mqtt_app_start();
}

static void on_wifi_down(void) {
  ESP_LOGI(TAG, "Lost connectivity");
  mqtt_app_stop();
}

static void on_provisioning_complete(const struct provisioning_config *cfg) {
  g_cfg = *cfg;
#if 1
  strcpy(g_cfg.mqtt_url, "mqtt://10.0.0.10:4500");
#endif
  g_cfg_ready = true;

  ESP_LOGI(TAG, "Provisioning received:");
  ESP_LOGI(TAG, "  this_device_name = %s", g_cfg.this_device_name);
  ESP_LOGI(TAG, "  ap_name          = %s", g_cfg.ap_name);
  ESP_LOGI(TAG, "  ap_pwd           = %s", g_cfg.ap_pwd);
  ESP_LOGI(TAG, "  mqtt_url         = %s", g_cfg.mqtt_url);
  ESP_LOGI(TAG, "  mqtt_usr         = %s", g_cfg.mqtt_usr);
  ESP_LOGI(TAG, "  mqtt_pwd         = %s", g_cfg.mqtt_pwd);

  struct wifi_cbs cbs = {.on_wifi_up = on_wifi_up, .on_wifi_down = on_wifi_down};
  const esp_err_t rc = wifi_connect(g_cfg.ap_name, g_cfg.ap_pwd, cbs);
  if (rc != ESP_OK) {
    ESP_LOGE(TAG, "wifi_connect failed: %s", esp_err_to_name(rc));
  }
}

static void on_boot_btn(bool active, void *usr) {
  (void)usr;
  provision_maybe_reset_btn_handler(active);
  c3_zero_led_blink(/*n=*/4, /*on_ms=*/200, /*off_ms=*/100, /*r=*/50, /*g=*/80, /*b=*/0);
}

void app_main(void);
void app_main(void) {
  // Quiet the IDF wifi driver — keeps WARN/ERROR, drops the verbose state-machine
  // chatter (channel transitions, beacon timing, etc).
  esp_log_level_set("wifi", ESP_LOG_WARN);
  esp_log_level_set("wifi_init", ESP_LOG_WARN);

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  g_mqtt_mutex = xSemaphoreCreateMutex();
  configASSERT(g_mqtt_mutex != NULL);

  // Start up: quick white flash to indicate we're starting main
  ESP_ERROR_CHECK(c3_zero_led_init());
  c3_zero_led_blink(/*n=*/1, /*on_ms=*/100, /*off_ms=*/100, /*r=*/50, /*g=*/50, /*b=*/50);

  const struct btn_mon_hanlder handlers[] = {
      {
          .gpio = BTN_GPIO,
          .active_high = false,
          .pull = BTN_MON_PULL_UP,
          .callback = &on_boot_btn,
      },
  };
  ESP_ERROR_CHECK(btn_mon_init(handlers, sizeof(handlers) / sizeof(handlers[0]), NULL));

  // Long-lived publisher task — no-ops until mqtt is connected.
  xTaskCreate(uptime_task, "uptime_pub", 4 * 1024, NULL, 3, NULL);

  ESP_ERROR_CHECK(wifi_provision_init(&on_provisioning_complete));
}
