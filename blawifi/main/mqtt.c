#include "mqtt.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdatomic.h>
#include <string.h>

#define TAG "blamqtt"

#define UPTIME_PUBLISH_INTERVAL_MS 5000

static char *g_topic;
static esp_mqtt_client_handle_t g_mqtt_client = NULL;
static atomic_bool g_mqtt_connected = false;

// -1 = never reported yet; otherwise 0/1 = latest presence value. Read on
// reconnect to re-sync the broker's retained message with our current state.
static atomic_int g_last_presence = -1;
static atomic_int g_last_batt = -1;

static void publish_presence(bool presence) {
  char topic[96];
  snprintf(topic, sizeof(topic), "%s/presence", g_topic);
  const char *payload = presence ? "1" : "0";
  // retain=1: broker keeps the value for new subscribers.
  // qos=1: esp-mqtt internally retries until ACKed; survives brief blips.
  esp_mqtt_client_publish(g_mqtt_client, topic, payload, 1, /*qos=*/1, /*retain=*/1);

  if (g_last_batt != -1) {
    snprintf(topic, sizeof(topic), "%s/battery", g_topic);
    char msg[20];
    snprintf(msg, sizeof(msg), "%d", g_last_batt);
    esp_mqtt_client_publish(g_mqtt_client, topic, msg, strlen(msg), /*qos=*/1, /*retain=*/1);
  }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  (void)handler_args;
  (void)base;
  esp_mqtt_event_handle_t event = event_data;
  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT CONNECTED");
    atomic_store(&g_mqtt_connected, true);
    // Resync the broker's retained presence with our current state — it may
    // be stale if presence flipped during the outage.
    {
      int v = atomic_load(&g_last_presence);
      if (v >= 0)
        publish_presence(v == 1);
    }
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT DISCONNECTED");
    atomic_store(&g_mqtt_connected, false);
    break;
  case MQTT_EVENT_ERROR:
    ESP_LOGW(TAG, "MQTT ERROR 0x%x", event->error_handle->error_type);
    atomic_store(&g_mqtt_connected, false);
    break;
  default:
    break;
  }
}

static void uptime_task(void *arg) {
  (void)arg;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(UPTIME_PUBLISH_INTERVAL_MS));
    if (!atomic_load(&g_mqtt_connected))
      continue;

    if (g_mqtt_client != NULL && atomic_load(&g_mqtt_connected)) {
      char topic[96];
      snprintf(topic, sizeof(topic), "%s/uptime", g_topic);
      char payload[24];
      int64_t s = esp_timer_get_time() / 1000000;
      int n = snprintf(payload, sizeof(payload), "%" PRId64, s);
      esp_mqtt_client_publish(g_mqtt_client, topic, payload, n, 0, 0);
    }

    if (g_last_batt != -1) {
      char topic[96];
      snprintf(topic, sizeof(topic), "%s/battery", g_topic);
      char msg[20];
      snprintf(msg, sizeof(msg), "%d", g_last_batt);
      esp_mqtt_client_publish(g_mqtt_client, topic, msg, strlen(msg), /*qos=*/1, /*retain=*/1);
    }
  }
}

esp_err_t mqtt_init(const char *mqtt_url, const char *mqtt_usr, const char *mqtt_pwd, const char *topic) {
  g_topic = strdup(topic);
  const esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = mqtt_url,
      .credentials.username = mqtt_usr,
      .credentials.authentication.password = mqtt_pwd,
  };
  g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  if (g_mqtt_client == NULL) {
    ESP_LOGE(TAG, "esp_mqtt_client_init failed");
    return ESP_FAIL;
  }

  esp_mqtt_client_register_event(g_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

  // Long-lived publisher task — no-ops until mqtt is connected.
  xTaskCreate(uptime_task, "uptime_pub", 4 * 1024, NULL, 3, NULL);
  return ESP_OK;
}

esp_err_t mqtt_start() {
  const esp_err_t rc = esp_mqtt_client_start(g_mqtt_client);
  if (rc != ESP_OK) {
    ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(rc));
    esp_mqtt_client_destroy(g_mqtt_client);
  }
  return rc;
}

void mqtt_report_presence(bool presence) {
  // Record the latest value first, so the reconnect handler can see it even
  // if we drop this publish below.
  atomic_store(&g_last_presence, presence ? 1 : 0);
  if (atomic_load(&g_mqtt_connected) && g_mqtt_client != NULL) {
    publish_presence(presence);
  }
  // else: drop. On MQTT_EVENT_CONNECTED the latest value is republished.
}

void mqtt_report_battery(int batt_mv) { atomic_store_explicit(&g_last_batt, batt_mv, memory_order_relaxed); }
