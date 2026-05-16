#include <string.h>

#include "c3_zero_led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"

static const char *TAG = "blabla";

#include "bt.h"
#include "bt_led.h"

#define MAX_ACTIVE_CONNS CONFIG_BT_NIMBLE_MAX_CONNECTIONS
static uint16_t g_active_conns[MAX_ACTIVE_CONNS];
static size_t g_active_conn_count = 0;
static SemaphoreHandle_t g_conns_lock = NULL;

static void conn_table_add(uint16_t handle) {
  xSemaphoreTake(g_conns_lock, portMAX_DELAY);
  if (g_active_conn_count < MAX_ACTIVE_CONNS) {
    g_active_conns[g_active_conn_count++] = handle;
  } else {
    ESP_LOGW(TAG, "conn table full, dropping handle=%u", handle);
  }
  xSemaphoreGive(g_conns_lock);
}

static void conn_table_remove(uint16_t handle) {
  xSemaphoreTake(g_conns_lock, portMAX_DELAY);
  for (size_t i = 0; i < g_active_conn_count; ++i) {
    if (g_active_conns[i] == handle) {
      g_active_conns[i] = g_active_conns[--g_active_conn_count];
      break;
    }
  }
  xSemaphoreGive(g_conns_lock);
}

void on_ble_sync_cb(uint8_t own_addr_type, uint8_t addr_val[6]) {
  c3_zero_led_blink(/*n=*/1, /*on_ms=*/500, /*off_ms=*/0, /*r=*/0, /*g=*/0,  /*b=*/255);
  bt_start_advertising();
}

void on_bt_conn_failed(const struct ble_gap_conn_desc *conn_desc) {
  c3_zero_led_blink(/*n=*/1, /*on_ms=*/500, /*off_ms=*/0, /*r=*/0, /*g=*/0,  /*b=*/90);
  bt_start_advertising();
}

void on_bt_new_conn(const struct ble_gap_conn_desc *conn_desc) {
  conn_table_add(conn_desc->conn_handle);
  c3_zero_led_blink(/*n=*/2, /*on_ms=*/200, /*off_ms=*/100, /*r=*/0, /*g=*/0,  /*b=*/90);
}

void on_bt_conn_bonded(const struct ble_gap_conn_desc *conn_desc) {
  c3_zero_led_blink(/*n=*/2, /*on_ms=*/200, /*off_ms=*/100, /*r=*/0, /*g=*/90,  /*b=*/0);
}

void on_bt_disconnect(const struct ble_gap_conn_desc *conn_desc) {
  conn_table_remove(conn_desc->conn_handle);
  c3_zero_led_blink(/*n=*/2, /*on_ms=*/500, /*off_ms=*/0, /*r=*/90, /*g=*/0,  /*b=*/0);
  bt_start_advertising();
}

void on_bt_adv_complete() { bt_start_advertising(); }

static void rssi_log_task(void *arg) {
  for (;;) {
    uint16_t snapshot[MAX_ACTIVE_CONNS];
    size_t count;

    xSemaphoreTake(g_conns_lock, portMAX_DELAY);
    count = g_active_conn_count;
    memcpy(snapshot, g_active_conns, count * sizeof(snapshot[0]));
    xSemaphoreGive(g_conns_lock);

    if (count == 0) {
      ESP_LOGI(TAG, "RSSI: no active connections");
    } else {
      for (size_t i = 0; i < count; ++i) {
        int8_t rssi = 0;
        int rc = ble_gap_conn_rssi(snapshot[i], &rssi);
        if (rc == 0) {
          ESP_LOGI(TAG, "RSSI: handle=%u %d dBm", snapshot[i], rssi);
        } else {
          ESP_LOGW(TAG, "RSSI: handle=%u read failed, rc=%d", snapshot[i], rc);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

static const struct bt_callbacks bt_cbs = {
    .on_ble_sync_cb = on_ble_sync_cb,
    .on_bt_conn_failed = on_bt_conn_failed,
    .on_bt_new_conn = on_bt_new_conn,
    .on_bt_conn_bonded = on_bt_conn_bonded,
    .on_bt_disconnect = on_bt_disconnect,
    .on_bt_adv_complete = on_bt_adv_complete,
};

void app_main() {
  c3_zero_led_init();
  c3_zero_led_clear();

  g_conns_lock = xSemaphoreCreateMutex();
  if (g_conns_lock == NULL) {
    ESP_LOGE(TAG, "failed to create conns mutex");
    return;
  }

  ESP_ERROR_CHECK(bt_init("blabla", "//nicolasbrailo.github.io", ESP_PWR_LVL_N24,
                          bt_cbs, bt_led_get_gatt_def()));

  if (xTaskCreate(rssi_log_task, "rssi_log", 3 * 1024, NULL, 3, NULL) != pdPASS) {
    ESP_LOGE(TAG, "failed to start rssi_log_task");
  }
}
