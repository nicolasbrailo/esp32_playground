#pragma once

#include "esp_bt.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdio.h>

struct ble_gap_event;
struct ble_gatt_svc_def;
struct ble_gap_conn_desc;

inline static void bt_addr_fmt(char *addr_str, uint8_t addr[]) {
  sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

struct bt_callbacks {
  void (*on_ble_sync_cb)(uint8_t own_addr_type, uint8_t addr_val[6]);
  void (*on_bt_conn_failed)(const struct ble_gap_conn_desc *conn_desc);
  void (*on_bt_new_conn)(const struct ble_gap_conn_desc *conn_desc);
  void (*on_bt_conn_bonded)(const struct ble_gap_conn_desc *conn_desc);
  void (*on_bt_disconnect)(const struct ble_gap_conn_desc *conn_desc);
  void (*on_bt_adv_complete)();
};

// tx_power: BLE TX power level, defaults to ESP_PWR_LVL_P3, min is ESP_PWR_LVL_N24
esp_err_t bt_init(const char *bt_dev_name, const char *adv_uri, esp_power_level_t tx_power, struct bt_callbacks cbs,
                  const struct ble_gatt_svc_def *gatt_svcs);
void bt_start_advertising();
