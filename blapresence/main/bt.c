#include "bt.h"

#include <stdlib.h>
#include <string.h>

#include "bt_helpers.h"
#include "custom_panic.h"
#include "esp_bt.h"
#include "esp_log.h"
static const char *TAG = "BT base";

#include "nvs_flash.h"

// Error codes for ble here https://mynewt.apache.org/latest/network/ble_hs/ble_hs_return_codes.html
#include "host/ble_hs.h"

#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* Quirk from Apache Mynewt NimBLE, there is no public definition for this because the project used a build hack to
 * regen headers that wasn't ported to ESP, and now the "standard" is to fwd declare and hope that nothing changed
 */
void ble_store_config_init(void);

#define BLE_GAP_APPEARANCE_GENERIC_HID 0x03C0
#define BLE_GAP_URI_PREFIX_HTTPS 0x17
#define BLE_GAP_LE_ROLE_PERIPHERAL 0x00

/* Advertised service UUIDs. HID Service (0x1812) tells the OS this is a HID
 * peripheral so it auto-reconnects after bonding. */
static const ble_uuid16_t g_adv_uuid16[] = {BLE_UUID16_INIT(0x1812)};

// BT addr once sync'd
static uint8_t g_own_addr_type;
static uint8_t g_addr_val[6] = {0};
struct bt_callbacks g_cbs;

static uint8_t *g_uri = NULL;
static size_t g_uri_len = 0;
static uint32_t g_passkey;
static esp_power_level_t g_tx_power;

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

  /* DEFAULT covers any power type not explicitly set, including future connections. */
  esp_err_t pwr_err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, g_tx_power);
  if (pwr_err != ESP_OK) {
    ESP_LOGE(TAG, "failed to set default BLE TX power: %d", pwr_err);
  }
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, g_tx_power);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, g_tx_power);
  ESP_LOGI(TAG, "Set BLE TX power to %d (%d dBm)", g_tx_power, -24 + 3 * (int)g_tx_power);

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

esp_err_t bt_init(const char *bt_dev_name, const char *adv_uri, esp_power_level_t tx_power, struct bt_callbacks cbs,
                  const struct ble_gatt_svc_def *gatt_svcs) {
  g_cbs = cbs;
  g_tx_power = tx_power;
  g_passkey = 123456;
  if (g_passkey < 100000 || g_passkey > 999999) {
    custom_panic("BT pin needs to be 6 digits");
  }

  size_t adv_uri_len = strlen(adv_uri);
  g_uri_len = adv_uri_len + 1;
  g_uri = malloc(g_uri_len);
  if (g_uri == NULL) {
    ESP_LOGE(TAG, "failed to allocate URI buffer");
    return ESP_ERR_NO_MEM;
  }
  g_uri[0] = BLE_GAP_URI_PREFIX_HTTPS;
  memcpy(&g_uri[1], adv_uri, adv_uri_len);

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

  /* Security manager configuration */
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 1;
  ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

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

  ESP_LOGI(TAG, "%s from remote addr %s %s (handle=%d, our_addr=%s)", trigger, remote_addr_str,
           event->connect.status == 0 ? "success" : "established", desc->conn_handle, local_addr_str);

  /* Connection info */
  ESP_LOGD(TAG,
           "conn_itvl=%d, conn_latency=%d, supervision_timeout=%d, "
           "encrypted=%d, authenticated=%d, bonded=%d\n",
           desc->conn_itvl, desc->conn_latency, desc->supervision_timeout, desc->sec_state.encrypted,
           desc->sec_state.authenticated, desc->sec_state.bonded);
}

static const char *gap_evt_str(struct ble_gap_event *evt) {
  switch (evt->type) {
  case BLE_GAP_EVENT_CONNECT:
    return "CONNECT";
  case BLE_GAP_EVENT_DISCONNECT:
    return "DISCONNECT";
  case BLE_GAP_EVENT_CONN_UPDATE:
    return "CONN_UPDATE";
  case BLE_GAP_EVENT_NOTIFY_TX:
    return "NOTIFY_TX";
  case BLE_GAP_EVENT_SUBSCRIBE:
    return "SUBSCRIBE";
  case BLE_GAP_EVENT_MTU:
    return "MTU";
  case BLE_GAP_EVENT_ENC_CHANGE:
    return "ENC_CHANGE";
  case BLE_GAP_EVENT_REPEAT_PAIRING:
    return "REPEAT_PAIRING";
  case BLE_GAP_EVENT_PASSKEY_ACTION:
    return "PASSKEY_ACTION";
  default:
    return NULL;
  }
}
static uint16_t gap_get_conn_hdl(struct ble_gap_event *evt) {
  switch (evt->type) {
  case BLE_GAP_EVENT_CONNECT:
    return evt->connect.conn_handle;
  case BLE_GAP_EVENT_CONN_UPDATE:
    return evt->conn_update.conn_handle;
  case BLE_GAP_EVENT_NOTIFY_TX:
    return evt->notify_tx.conn_handle;
  case BLE_GAP_EVENT_SUBSCRIBE:
    return evt->subscribe.conn_handle;
  case BLE_GAP_EVENT_MTU:
    return evt->mtu.conn_handle;
  case BLE_GAP_EVENT_ENC_CHANGE:
    return evt->enc_change.conn_handle;
  case BLE_GAP_EVENT_REPEAT_PAIRING:
    return evt->repeat_pairing.conn_handle;
  case BLE_GAP_EVENT_PASSKEY_ACTION:
    return evt->passkey.conn_handle;
  default:
    return -1;
  }
}

// Same as gap_event_handler but the events here must have a valid connection
static int gap_conn_event_handler(struct ble_gap_event *event, void *arg) {
  if (gap_evt_str(event) == NULL) {
    // Unmapped event that we can safely ignore
    return 0;
  }

  struct ble_gap_conn_desc desc;
  int rc = ble_gap_conn_find(gap_get_conn_hdl(event), &desc);
  if (rc != 0) {
    ESP_LOGE(TAG, "%s received, failed to find connection by handle, error: %d", gap_evt_str(event), rc);
    return rc;
  }

  switch (event->type) {

  case BLE_GAP_EVENT_CONNECT:
    print_conn_desc("New connection", event, &desc);

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

  /* Connection parameters update event */
  case BLE_GAP_EVENT_CONN_UPDATE:
    print_conn_desc("Updated connection", event, &desc);
    return 0;

  case BLE_GAP_EVENT_PASSKEY_ACTION:
    print_conn_desc("Bond request", event, &desc);
    if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
      /* Generate passkey */
      struct ble_sm_io pkey = {0};
      pkey.action = event->passkey.params.action;
      pkey.passkey = g_passkey;
      ESP_LOGI(TAG, "enter passkey %" PRIu32 " on the peer side", pkey.passkey);
      rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
      if (rc != 0) {
        ESP_LOGE(TAG, "failed to inject security manager io, error code: %d", rc);
        return rc;
      }
    }
    return rc;

  case BLE_GAP_EVENT_ENC_CHANGE:
    if (event->enc_change.status == 0) {
      print_conn_desc("Encryption enabled", event, &desc);
      g_cbs.on_bt_conn_bonded(&desc);
    } else {
      print_conn_desc("Encryption disabled", event, &desc);
    }
    return 0;
  case BLE_GAP_EVENT_REPEAT_PAIRING:
    print_conn_desc("Repeat pairing ", event, &desc);
    ble_store_util_delete_peer(&desc.peer_id_addr);
    /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
     * continue with pairing operation */
    return BLE_GAP_REPEAT_PAIRING_RETRY;

  default:
    ESP_LOGE(TAG, "XXX TODO DEFAULT %d", event->type);
    return 0;
  }
}

// GAP events that don't require a valid connection handle
static int gap_event_handler(struct ble_gap_event *event, void *arg) {
  /* Handle different GAP event */
  switch (event->type) {
  /* Disconnect event */
  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "Disconnected from peer; reason=%d (%s)", event->disconnect.reason,
             bt_hci_disconnect_reason_str(event->disconnect.reason));
    g_cbs.on_bt_disconnect(&event->disconnect.conn);
    return 0;

  /* Advertising complete event */
  case BLE_GAP_EVENT_ADV_COMPLETE:
    ESP_LOGI(TAG, "Advertise complete; reason=%d", event->adv_complete.reason);
    g_cbs.on_bt_adv_complete();
    return 0;

  /* Notification sent event */
  case BLE_GAP_EVENT_NOTIFY_TX:
    if ((event->notify_tx.status != 0) && (event->notify_tx.status != BLE_HS_EDONE)) {
      /* Print notification info on error */
      ESP_LOGD(TAG,
               "notify event; conn_handle=%d attr_handle=%d "
               "status=%d (%s) is_indication=%d",
               event->notify_tx.conn_handle, event->notify_tx.attr_handle, event->notify_tx.status,
               bt_att_err_str(event->notify_tx.status), event->notify_tx.indication);
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

  default:
    return gap_conn_event_handler(event, arg);
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

  /* Set device appearance: Generic HID, so phones treat us as a HID peripheral. */
  adv_fields.appearance = BLE_GAP_APPEARANCE_GENERIC_HID;
  adv_fields.appearance_is_present = 1;

  /* Advertise the HID service UUID so scanners (and phones' BT stacks) recognise
   * this as a HID device and enable their auto-reconnect path. */
  adv_fields.uuids16 = g_adv_uuid16;
  adv_fields.num_uuids16 = sizeof(g_adv_uuid16) / sizeof(g_adv_uuid16[0]);
  adv_fields.uuids16_is_complete = 1;

  /* Set device LE role */
  adv_fields.le_role = BLE_GAP_LE_ROLE_PERIPHERAL;
  adv_fields.le_role_is_present = 1;

  /* Set advertisement fields */
  rc = ble_gap_adv_set_fields(&adv_fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to set advertising data, error code: %d", rc);
    return;
  }

  /* Set device address: the example does this, but it's not needed. Addr is already part of the phy header */
  // rsp_fields.device_addr = g_addr_val;
  // rsp_fields.device_addr_type = g_own_addr_type;
  // rsp_fields.device_addr_is_present = 1;

  /* Set advertising interval: similar to device addr, the other end doesn't care about our adv itvl, especially after
   * it already scanned us */
  // rsp_fields.adv_itvl = BLE_GAP_ADV_ITVL_MS(500);
  // rsp_fields.adv_itvl_is_present = 1;

  // If adv is too big and the panic belows triggers, it can be commented out
  rsp_fields.uri = g_uri;
  rsp_fields.uri_len = g_uri_len;

  /* Set scan response fields */
  rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
  if (rc == BLE_HS_EMSGSIZE) {
    custom_panic("BLE advertise message too big. You may want to drop the URI from the BT advertisement.");
  }
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to set scan response data, error code: %d", rc);
    return;
  }

  /* Set undirected connectable and general discoverable mode */
  // https://mynewt.apache.org/latest/network/ble_hs/ble_gap.html#c.ble_gap_adv_params
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  /* Set advertising interval */
  // TODO - change intervals to see how quickly a device can find it after scanning
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
