#include "c3_zero_led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// static const char *TAG = "blabla";

#include "bt.h"
#include "bt_led.h"

void on_ble_sync_cb(uint8_t own_addr_type, uint8_t addr_val[6]) {
  c3_zero_led_blink(/*n=*/1, /*on_ms=*/500, /*off_ms=*/0, /*r=*/0, /*g=*/0,  /*b=*/255);
  bt_start_advertising();
}

void on_bt_conn_failed(const struct ble_gap_conn_desc *conn_desc) {
  c3_zero_led_blink(/*n=*/1, /*on_ms=*/500, /*off_ms=*/0, /*r=*/0, /*g=*/0,  /*b=*/90);
  bt_start_advertising();
}

void on_bt_new_conn(const struct ble_gap_conn_desc *conn_desc) {
  c3_zero_led_blink(/*n=*/2, /*on_ms=*/200, /*off_ms=*/100, /*r=*/0, /*g=*/0,  /*b=*/90);
}

void on_bt_conn_bonded(const struct ble_gap_conn_desc *conn_desc) {
  c3_zero_led_blink(/*n=*/2, /*on_ms=*/200, /*off_ms=*/100, /*r=*/0, /*g=*/90,  /*b=*/0);
}

void on_bt_disconnect(const struct ble_gap_conn_desc *conn_desc) {
  c3_zero_led_blink(/*n=*/2, /*on_ms=*/500, /*off_ms=*/0, /*r=*/90, /*g=*/0,  /*b=*/0);
  bt_start_advertising();
}

void on_bt_adv_complete() { bt_start_advertising(); }

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
  c3_zero_led_clear(); // Reset in case a previous run left this on
  ESP_ERROR_CHECK(bt_init("blabla", "//nicolasbrailo.github.io", bt_cbs, bt_led_get_gatt_def()));
}
