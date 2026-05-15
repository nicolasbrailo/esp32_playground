#pragma once

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
  void (*on_bt_disconnect)();
  void (*on_bt_adv_complete)();
};

esp_err_t bt_init(const char *bt_dev_name, struct bt_callbacks cbs, const struct ble_gatt_svc_def *gatt_svcs);
void bt_start_advertising();
