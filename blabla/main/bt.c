#include "bt.h"

#include "esp_log.h"
static const char *TAG = "BT base";

#include "nvs_flash.h"

#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* Quirk from Apache Mynewt NimBLE, there is no public definition for this because the project used a build hack to
 * regen headers that wasn't ported to ESP, and now the "standard" is to fwd declare and hope that nothing changed
 */
void ble_store_config_init(void);

#define BLE_GAP_APPEARANCE_GENERIC_TAG 0x0200
#define BLE_GAP_URI_PREFIX_HTTPS 0x17
#define BLE_GAP_LE_ROLE_PERIPHERAL 0x00

// BT addr once sync'd
static uint8_t g_own_addr_type;
static uint8_t g_addr_val[6] = {0};
struct bt_callbacks g_cbs;

// BLE stack is working and ready
static void on_ble_sync(void) {
  int rc = 0;
  /* Make sure we have proper BT identity address set (random preferred) */
  rc = ble_hs_util_ensure_addr(0);
  if (rc != 0) {
    ESP_LOGE(TAG, "NimBLE announced sync, but device has no BT address");
    return;
  }

  /* Figure out BT address to use while advertising */
  rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to infer address type, error code: %d", rc);
    return;
  }

  /* Printing ADDR */
  rc = ble_hs_id_copy_addr(g_own_addr_type, g_addr_val, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to copy device address, error code: %d", rc);
    return;
  }

  char addr_str[18] = {0};
  bt_addr_fmt(addr_str, g_addr_val);
  ESP_LOGI(TAG, "NimBLE BLE stack sync, address %s", addr_str);
  g_cbs.on_ble_sync_cb(g_own_addr_type, g_addr_val);
}

// BLE failed to setup
static void on_ble_reset(int reason) {
  ESP_LOGI(TAG, "NimBLE reset, reason: %d", reason);
  // Not much we can do, but we could hook an error CB here
}

// BG task
static void nimble_host_task(void *param) {
  ESP_LOGI(TAG, "NimBLE host task started");
  nimble_port_run(); // Blocks until nimble_port_stop called
  ESP_LOGI(TAG, "NimBLE host task stopped");
  vTaskDelete(NULL);
}

esp_err_t bt_init(const char *bt_dev_name, struct bt_callbacks cbs, const struct ble_gatt_svc_def *gatt_svcs) {
  g_cbs = cbs;

  // NVS flash initialization, needed for BLE stack to store config+runtime state
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize NVS flash, error: %d ", ret);
    return ret;
  }

  ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to initialize NimBLE stack, error: %d ", ret);
    return ret;
  }

  // Set up basic BLE callbacks
  ble_hs_cfg.reset_cb = on_ble_reset;
  ble_hs_cfg.sync_cb = on_ble_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ble_store_config_init();

  // Init GAP
  ble_svc_gap_init();
  int rc = ble_svc_gap_device_name_set(bt_dev_name);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to set device name to %s, error code: %d", bt_dev_name, rc);
    return rc;
  }

  // Init GATT
  ble_svc_gatt_init();
  if (ble_gatts_count_cfg(gatt_svcs) != 0) {
    ESP_LOGE(TAG, "Failed ble_gatts_count_cfg");
    return ESP_FAIL;
  }
  if (ble_gatts_add_svcs(gatt_svcs) != 0) {
    ESP_LOGE(TAG, "Failed ble_gatts_add_svcs");
    return ESP_FAIL;
  }

  // Launch task for BLE. This should call host_task, which should run NimBLE until it sync's with the host
  if (xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL) != pdPASS) {
    ESP_LOGE(TAG, "failed to create NimBLE host task");
    return ESP_FAIL;
  }

  return ESP_OK;
}

static void print_conn_desc(const char *trigger, struct ble_gap_event *event, struct ble_gap_conn_desc *desc) {
  char local_addr_str[18] = {0};
  bt_addr_fmt(local_addr_str, desc->our_id_addr.val);

  char remote_addr_str[18] = {0};
  bt_addr_fmt(remote_addr_str, desc->peer_id_addr.val);

  ESP_LOGI(TAG, "%s connection to remote addr %s %s (handle=%d, our_addr=%s)", trigger, remote_addr_str,
           event->connect.status == 0 ? "established" : "success", desc->conn_handle, local_addr_str);

  /* Connection info */
  ESP_LOGD(TAG,
           "conn_itvl=%d, conn_latency=%d, supervision_timeout=%d, "
           "encrypted=%d, authenticated=%d, bonded=%d\n",
           desc->conn_itvl, desc->conn_latency, desc->supervision_timeout, desc->sec_state.encrypted,
           desc->sec_state.authenticated, desc->sec_state.bonded);
}

static int gap_event_handler(struct ble_gap_event *event, void *arg) {
  int rc = 0;
  struct ble_gap_conn_desc desc;

  /* Handle different GAP event */
  switch (event->type) {

  case BLE_GAP_EVENT_CONNECT:
    rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
    if (rc != 0) {
      // TODO will this work for a failed connection?
      ESP_LOGE(TAG, "Connection event received, but failed to find its handle, error: %d", rc);
      return rc;
    }
    print_conn_desc("New", event, &desc);

    /* Connection succeeded */
    if (event->connect.status == 0) {
      struct ble_gap_upd_params params = {.itvl_min = desc.conn_itvl,
                                          .itvl_max = desc.conn_itvl,
                                          .latency = 3,
                                          .supervision_timeout = desc.supervision_timeout};
      rc = ble_gap_update_params(event->connect.conn_handle, &params);
      if (rc != 0) {
        ESP_LOGE(TAG, "failed to update connection parameters, error code: %d", rc);
        return rc;
      }

      g_cbs.on_bt_new_conn(&desc);
    }
    /* Connection failed, restart advertising */
    else {
      g_cbs.on_bt_conn_failed(&desc);
    }
    return 0;

  /* Disconnect event */
  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "Disconnected from peer; reason=%d", event->disconnect.reason);
    // TODO find client that disconnected here
    g_cbs.on_bt_disconnect();
    return 0;

  /* Connection parameters update event */
  case BLE_GAP_EVENT_CONN_UPDATE:
    rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
    if (rc != 0) {
      ESP_LOGE(TAG, "Connection updated, but failed to by handle, error: %d", rc);
      return rc;
    }
    print_conn_desc("Updated", event, &desc);
    return 0;

  /* Advertising complete event */
  case BLE_GAP_EVENT_ADV_COMPLETE:
    /* Advertising completed, restart advertising */
    ESP_LOGI(TAG, "Advertise complete; reason=%d", event->adv_complete.reason);
    g_cbs.on_bt_adv_complete();
    return 0;

  /* Notification sent event */
  case BLE_GAP_EVENT_NOTIFY_TX:
    if ((event->notify_tx.status != 0) && (event->notify_tx.status != BLE_HS_EDONE)) {
      /* Print notification info on error */
      ESP_LOGD(TAG,
               "notify event; conn_handle=%d attr_handle=%d "
               "status=%d is_indication=%d",
               event->notify_tx.conn_handle, event->notify_tx.attr_handle, event->notify_tx.status,
               event->notify_tx.indication);
    }
    return 0;

  /* Subscribe event */
  case BLE_GAP_EVENT_SUBSCRIBE:
    /* Print subscription info to log */
    ESP_LOGD(TAG,
             "subscribe event; conn_handle=%d attr_handle=%d "
             "reason=%d prevn=%d curn=%d previ=%d curi=%d",
             event->subscribe.conn_handle, event->subscribe.attr_handle, event->subscribe.reason,
             event->subscribe.prev_notify, event->subscribe.cur_notify, event->subscribe.prev_indicate,
             event->subscribe.cur_indicate);

    // If we are streaming data, this would be the place to handle it (eg a temp sensor)
    // See gatt_svr_subscribe_cb in NimBLE_GATT_Server example
    return 0;

  /* MTU update event */
  case BLE_GAP_EVENT_MTU:
    /* Print MTU update info to log */
    ESP_LOGD(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d", event->mtu.conn_handle, event->mtu.channel_id,
             event->mtu.value);
    return 0;
  }

  return 0;
}

void bt_start_advertising() {
  int rc;
  const char *name;
  struct ble_hs_adv_fields adv_fields = {0};
  struct ble_hs_adv_fields rsp_fields = {0};
  struct ble_gap_adv_params adv_params = {0};

  /* Set advertising flags */
  adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  /* Set device name */
  name = ble_svc_gap_device_name();
  adv_fields.name = (uint8_t *)name;
  adv_fields.name_len = strlen(name);
  adv_fields.name_is_complete = 1;

  /* Set device tx power */
  adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
  adv_fields.tx_pwr_lvl_is_present = 1;

  /* Set device appearance */
  adv_fields.appearance = BLE_GAP_APPEARANCE_GENERIC_TAG;
  adv_fields.appearance_is_present = 1;

  /* Set device LE role */
  adv_fields.le_role = BLE_GAP_LE_ROLE_PERIPHERAL;
  adv_fields.le_role_is_present = 1;

  /* Set advertisement fields */
  rc = ble_gap_adv_set_fields(&adv_fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to set advertising data, error code: %d", rc);
    return;
  }

  /* Set device address */
  rsp_fields.device_addr = g_addr_val;
  rsp_fields.device_addr_type = g_own_addr_type;
  rsp_fields.device_addr_is_present = 1;

  /* TODO: Set URI */
  static uint8_t g_uri[] = {BLE_GAP_URI_PREFIX_HTTPS, 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm'};
  rsp_fields.uri = g_uri;
  rsp_fields.uri_len = sizeof(g_uri);

  /* Set advertising interval */
  rsp_fields.adv_itvl = BLE_GAP_ADV_ITVL_MS(500);
  rsp_fields.adv_itvl_is_present = 1;

  /* Set scan response fields */
  rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to set scan response data, error code: %d", rc);
    return;
  }

  /* Set undirected connectable and general discoverable mode */
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  /* Set advertising interval */
  adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(500);
  adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(510);

  /* Start advertising */
  rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event_handler, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to start advertising, error code: %d", rc);
    return;
  }
  ESP_LOGI(TAG, "Now advertising BT");
}
