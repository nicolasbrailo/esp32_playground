#include "bt_gatt_read_id.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"

struct bt_name_read_state {
  bt_name_read_cb user_cb;
  void *user_arg;
  uint16_t val_handle;
  bool svc_found;
  bool chr_found;
};

static const ble_uuid16_t BT_GAP_SVC_UUID = BLE_UUID16_INIT(0x1800);
static const ble_uuid16_t BT_DEVICE_NAME_CHR_UUID = BLE_UUID16_INIT(0x2A00);

static void bt_name_read_finish_(uint16_t conn_handle, struct bt_name_read_state *s, const char *name, size_t len) {
  s->user_cb(conn_handle, name, len, s->user_arg);
  free(s);
}

static int bt_name_read_value_cb_(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr,
                                  void *arg) {
  struct bt_name_read_state *s = arg;
  if (error->status == 0 && attr != NULL && attr->om != NULL) {
    char buf[64];
    uint16_t len = OS_MBUF_PKTLEN(attr->om);
    if (len >= sizeof(buf)) {
      len = sizeof(buf) - 1;
    }
    os_mbuf_copydata(attr->om, 0, len, buf);
    buf[len] = '\0';
    bt_name_read_finish_(conn_handle, s, buf, len);
  } else {
    bt_name_read_finish_(conn_handle, s, NULL, 0);
  }
  return 0;
}

static int bt_name_read_chr_cb_(uint16_t conn_handle, const struct ble_gatt_error *error,
                                const struct ble_gatt_chr *chr, void *arg) {
  struct bt_name_read_state *s = arg;
  if (error->status == 0 && chr != NULL && !s->chr_found) {
    s->chr_found = true;
    s->val_handle = chr->val_handle;
    return 0;
  }
  if (error->status == BLE_HS_EDONE) {
    if (!s->chr_found) {
      bt_name_read_finish_(conn_handle, s, NULL, 0);
      return 0;
    }
    int rc = ble_gattc_read(conn_handle, s->val_handle, bt_name_read_value_cb_, s);
    if (rc != 0) {
      bt_name_read_finish_(conn_handle, s, NULL, 0);
    }
  }
  return 0;
}

static int bt_name_read_svc_cb_(uint16_t conn_handle, const struct ble_gatt_error *error,
                                const struct ble_gatt_svc *svc, void *arg) {
  struct bt_name_read_state *s = arg;
  if (error->status == 0 && svc != NULL && !s->svc_found) {
    s->svc_found = true;
    int rc = ble_gattc_disc_chrs_by_uuid(conn_handle, svc->start_handle, svc->end_handle, &BT_DEVICE_NAME_CHR_UUID.u,
                                         bt_name_read_chr_cb_, s);
    if (rc != 0) {
      bt_name_read_finish_(conn_handle, s, NULL, 0);
    }
    return 0;
  }
  if (error->status == BLE_HS_EDONE && !s->svc_found) {
    bt_name_read_finish_(conn_handle, s, NULL, 0);
  }
  return 0;
}

int bt_gatt_read_device_name(uint16_t conn_handle, bt_name_read_cb cb, void *user_arg) {
  struct bt_name_read_state *s = calloc(1, sizeof(*s));
  if (s == NULL) {
    return BLE_HS_ENOMEM;
  }
  s->user_cb = cb;
  s->user_arg = user_arg;
  int rc = ble_gattc_disc_svc_by_uuid(conn_handle, &BT_GAP_SVC_UUID.u, bt_name_read_svc_cb_, s);
  if (rc != 0) {
    free(s);
  }
  return rc;
}
