#include "bt_hid.h"

#include <stdint.h>

#include "esp_log.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"

static const char *TAG = "BT HID";

/* Service / characteristic UUIDs from BLE Assigned Numbers. */
static const ble_uuid16_t UUID_HID_SVC = BLE_UUID16_INIT(0x1812);
static const ble_uuid16_t UUID_HID_INFO = BLE_UUID16_INIT(0x2A4A);
static const ble_uuid16_t UUID_HID_CONTROL_POINT = BLE_UUID16_INIT(0x2A4C);
static const ble_uuid16_t UUID_HID_REPORT_MAP = BLE_UUID16_INIT(0x2A4B);
static const ble_uuid16_t UUID_HID_REPORT = BLE_UUID16_INIT(0x2A4D);
static const ble_uuid16_t UUID_HID_PROTOCOL_MODE = BLE_UUID16_INIT(0x2A4E);
static const ble_uuid16_t UUID_REPORT_REFERENCE = BLE_UUID16_INIT(0x2908);

static const ble_uuid16_t UUID_DIS_SVC = BLE_UUID16_INIT(0x180A);
static const ble_uuid16_t UUID_PNP_ID = BLE_UUID16_INIT(0x2A50);

static const ble_uuid16_t UUID_BAT_SVC = BLE_UUID16_INIT(0x180F);
static const ble_uuid16_t UUID_BAT_LEVEL = BLE_UUID16_INIT(0x2A19);

static uint16_t hid_report_val_handle;

static int access_hid_info(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  /* bcdHID = 0x0111 (HID 1.11), CountryCode = 0 (not localized), Flags = RemoteWake|NormallyConnectable. */
  static const uint8_t info[] = {0x11, 0x01, 0x00, 0x03};
  return os_mbuf_append(ctxt->om, info, sizeof(info)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int access_hid_control_point(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                    void *arg) {
  /* Host writes 0x00 (Suspend) / 0x01 (Exit Suspend). We don't care — accept silently. */
  return 0;
}

static int access_hid_report_map(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                 void *arg) {
  /* Minimal Consumer Control Report Map: one Input Report (ID=1) carrying a
   * 16-bit consumer usage code. We never transmit it; the descriptor exists only
   * to convince iOS/Android we are a valid HID Consumer device. iOS in
   * particular refuses to bind to a HID device with a malformed/empty Report
   * Map, so this minimal-but-spec-clean descriptor matters. */
  static const uint8_t consumer_report_map[] = {
      0x05, 0x0C,       // Usage Page (Consumer: media keys, app launchers, etc)
      0x09, 0x01,       // Usage (Consumer Control, a media-remote-like device)
      0xA1, 0x01,       // Collection (Application)
      0x85, 0x01,       //   Report ID (1)
      0x15, 0x00,       //   Logical Minimum (0)
      0x26, 0xFF, 0x03, //   Logical Maximum (0x03FF)
      0x19, 0x00,       //   Usage Minimum (0)
      0x2A, 0xFF, 0x03, //   Usage Maximum (0x03FF)
      0x75, 0x10,       //   Report Size (16)
      0x95, 0x01,       //   Report Count (1)
      0x81, 0x00,       //   Input (Data, Array, Absolute)
      0xC0,             // End Collection
  };
  const int rc = os_mbuf_append(ctxt->om, consumer_report_map, sizeof(consumer_report_map));
  return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int access_hid_report(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  /* If a host reads the input report, return "no button pressed". */
  static const uint8_t empty[2] = {0, 0};
  return os_mbuf_append(ctxt->om, empty, sizeof(empty)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int access_report_reference(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                   void *arg) {
  /* Report ID = 1, Report Type = 1 (Input). */
  static const uint8_t ref[] = {0x01, 0x01};
  return os_mbuf_append(ctxt->om, ref, sizeof(ref)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int access_hid_protocol_mode(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                    void *arg) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    static uint8_t hid_protocol_mode = 0x01; // 1 = Report Protocol (default for non-boot HID)
    return os_mbuf_append(ctxt->om, &hid_protocol_mode, 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  ESP_LOGW(TAG, "A device attempted to request boot-mode for this device, ignoring");
  return 0;
}

static int access_pnp_id(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  static const uint8_t pnp[] = {
      0x01, // Vendor source = 0x01 (Bluetooth SIG)
      0xFF, 0xFF, // Vendor = 0xFFFF (BT SIG-reserved for tests, a "not a real company" ID)
      0xCA, 0xFE, // Product ID
      0x01, 0x00  // Version ID
  };
  return os_mbuf_append(ctxt->om, pnp, sizeof(pnp)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int access_battery_level(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                void *arg) {
  /* Hardcoded. iOS shows this in the BT settings widget for HID devices. */
  static const uint8_t level = 42;
  return os_mbuf_append(ctxt->om, &level, 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    /* HID over GATT Service (mandatory). */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_HID_SVC.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &UUID_HID_INFO.u,
                    .access_cb = access_hid_info,
                    .flags = BLE_GATT_CHR_F_READ,
                },
                {
                    .uuid = &UUID_HID_CONTROL_POINT.u,
                    .access_cb = access_hid_control_point,
                    .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
                },
                {
                    .uuid = &UUID_HID_REPORT_MAP.u,
                    .access_cb = access_hid_report_map,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
                },
                {
                    .uuid = &UUID_HID_REPORT.u,
                    .access_cb = access_hid_report,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
                    .val_handle = &hid_report_val_handle,
                    .descriptors =
                        (struct ble_gatt_dsc_def[]){
                            {
                                .uuid = &UUID_REPORT_REFERENCE.u,
                                .att_flags = BLE_ATT_F_READ,
                                .access_cb = access_report_reference,
                            },
                            {0},
                        },
                },
                {
                    .uuid = &UUID_HID_PROTOCOL_MODE.u,
                    .access_cb = access_hid_protocol_mode,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
                },
                {0},
            },
    },

    /* Device Information Service (PnP ID is required by HOGP). */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_DIS_SVC.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &UUID_PNP_ID.u,
                    .access_cb = access_pnp_id,
                    .flags = BLE_GATT_CHR_F_READ,
                },
                {0},
            },
    },

    /* Battery Service (recommended by HOGP; iOS expects it). */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_BAT_SVC.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &UUID_BAT_LEVEL.u,
                    .access_cb = access_battery_level,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                },
                {0},
            },
    },

    {0},
};

const struct ble_gatt_svc_def *bt_hid_get_gatt_def() {
  ESP_LOGI(TAG, "Set up HID GATT table");
  return gatt_svcs;
}
