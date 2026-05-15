#include "c3_zero_led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// static const char *TAG = "blabla";

#include "bt.h"
#include "bt_led.h"

void on_ble_sync_cb(uint8_t own_addr_type, uint8_t addr_val[6]) { bt_start_advertising(); }
void on_bt_conn_failed(const struct ble_gap_conn_desc *conn_desc) { bt_start_advertising(); }
void on_bt_new_conn(const struct ble_gap_conn_desc *conn_desc) {}
void on_bt_disconnect() { bt_start_advertising(); }
void on_bt_adv_complete() { bt_start_advertising(); }

static const struct bt_callbacks bt_cbs = {
    .on_ble_sync_cb = on_ble_sync_cb,
    .on_bt_conn_failed = on_bt_conn_failed,
    .on_bt_new_conn = on_bt_new_conn,
    .on_bt_disconnect = on_bt_disconnect,
    .on_bt_adv_complete = on_bt_adv_complete,
};

void app_main() {
  c3_zero_led_init();
  c3_zero_led_clear(); // Reset in case a previous run left this on
  ESP_ERROR_CHECK(bt_init("blabla", bt_cbs, bt_led_get_gatt_def()));
}
