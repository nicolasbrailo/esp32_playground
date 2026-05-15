#include "c3_zero_led.h"
#include "esp_log.h"
static const char *TAG = "BT LED CTRL";

#include "nvs_flash.h"

#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* Automation IO service */
static uint16_t led_chr_val_handle;
static const ble_uuid16_t g_uuid16_svc_automation_io = BLE_UUID16_INIT(0x1815);
static const ble_uuid128_t led_chr_uuid =
    BLE_UUID128_INIT(0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15, 0xde, 0xef, 0x12, 0x12, 0x25, 0x15, 0x00, 0x00);
static int on_io_scv_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

/* Color service */
static const ble_uuid128_t g_uuid16_svc_led_color =
    BLE_UUID128_INIT(0x23, 0x76, 0x57, 0x5d, 0xa4, 0x6d, 0x46, 0x37, 0xa6, 0xc0, 0xb9, 0x03, 0xf1, 0x40, 0x89, 0x91);
static const ble_uuid128_t r_chr_uuid =
    BLE_UUID128_INIT(0x3d, 0x2f, 0x01, 0x26, 0x89, 0x6f, 0x40, 0xa0, 0xb7, 0xa4, 0x5f, 0xea, 0xf7, 0x38, 0x02, 0x24);
static const ble_uuid128_t g_chr_uuid =
    BLE_UUID128_INIT(0x0d, 0xde, 0xda, 0x24, 0xfe, 0x1e, 0x41, 0x7c, 0xaa, 0x21, 0x3a, 0xe9, 0xb0, 0x9b, 0x7f, 0x31);
static const ble_uuid128_t b_chr_uuid =
    BLE_UUID128_INIT(0xcc, 0xca, 0x67, 0x45, 0x1e, 0x88, 0x45, 0x0a, 0x8c, 0x96, 0xb2, 0xf5, 0xeb, 0xad, 0x1d, 0x98);
static int on_color_scv_access_val(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                   void *arg);
static int on_color_scv_access_name(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                    void *arg);
static int on_color_scv_access_format(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                      void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    /* Automation IO service */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &g_uuid16_svc_automation_io.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){/* LED characteristic */
                                        {.uuid = &led_chr_uuid.u,
                                         .access_cb = on_io_scv_access,
                                         .flags = BLE_GATT_CHR_F_WRITE,
                                         .val_handle = &led_chr_val_handle},
                                        {0}},
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &g_uuid16_svc_led_color.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {.uuid = &r_chr_uuid.u,
                 .access_cb = on_color_scv_access_val,
                 .arg = "Red",
                 .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ,
                 .descriptors =
                     (struct ble_gatt_dsc_def[]){
                         {
                             .uuid = BLE_UUID16_DECLARE(0x2901), /* CUD Characteristic User Description */
                             .att_flags = BLE_ATT_F_READ,
                             .access_cb = on_color_scv_access_name,
                             .arg = "Red",
                         },
                         {
                             .uuid = BLE_UUID16_DECLARE(0x2904), /* CPF, Characteristic Presentation Format */
                             .att_flags = BLE_ATT_F_READ,
                             .access_cb = on_color_scv_access_format,
                             .arg = "Red",
                         },
                         {0}}},
                {.uuid = &g_chr_uuid.u,
                 .access_cb = on_color_scv_access_val,
                 .arg = "Green",
                 .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ,
                 .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                .uuid = BLE_UUID16_DECLARE(0x2901),
                                                                .att_flags = BLE_ATT_F_READ,
                                                                .access_cb = on_color_scv_access_name,
                                                                .arg = "Green",
                                                            },
                                                            {
                                                                .uuid = BLE_UUID16_DECLARE(0x2904),
                                                                .att_flags = BLE_ATT_F_READ,
                                                                .access_cb = on_color_scv_access_format,
                                                                .arg = "Green",
                                                            },
                                                            {0}}},
                {.uuid = &b_chr_uuid.u,
                 .access_cb = on_color_scv_access_val,
                 .arg = "Blue",
                 .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ,
                 .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                .uuid = BLE_UUID16_DECLARE(0x2901),
                                                                .att_flags = BLE_ATT_F_READ,
                                                                .access_cb = on_color_scv_access_name,
                                                                .arg = "Blue",
                                                            },
                                                            {
                                                                .uuid = BLE_UUID16_DECLARE(0x2904),
                                                                .att_flags = BLE_ATT_F_READ,
                                                                .access_cb = on_color_scv_access_format,
                                                                .arg = "Blue",
                                                            },
                                                            {0}}},
                {0}},
    },

    {0}, /* No more services. */
};

const struct ble_gatt_svc_def *bt_led_get_gatt_def() { return gatt_svr_svcs; }

static uint8_t g_led_on = 0, g_color_r = 255, g_color_g = 255, g_color_b = 255;
static int on_io_scv_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  ESP_LOGI(TAG, "IO SVC ACCESS");
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    ESP_LOGE(TAG, "Unsupported op %d for on_io_scv_access", ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
  }
  if (attr_handle != led_chr_val_handle) {
    ESP_LOGE(TAG, "Invalid handle for on_io_scv_access");
    return BLE_ATT_ERR_UNLIKELY;
  }
  if (ctxt->om->om_len < 1) {
    ESP_LOGE(TAG, "Invalid op args len on_io_scv_access");
    return BLE_ATT_ERR_UNLIKELY;
  }

  g_led_on = ctxt->om->om_data[0];
  if (g_led_on) {
    ESP_LOGI(TAG, "led turned on!");
    c3_zero_led_set(g_color_r, g_color_g, g_color_b);
  } else {
    ESP_LOGI(TAG, "led turned off!");
    c3_zero_led_clear();
  }
  return 0;
}

static int on_color_scv_access_name(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                    void *arg) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
    const char *col = arg;
    if (!col) {
      ESP_LOGE(TAG, "Invalid NULL arg for on_color_scv_access_name");
      return BLE_ATT_ERR_UNLIKELY;
    }

    // Copy the name as is, we don't need to worry which callback was invoked
    os_mbuf_append(ctxt->om, col, strlen(col));
    return 0;
  }

  ESP_LOGW(TAG, "Unsupported op %d for on_color_scv_access_name", ctxt->op);
  return BLE_ATT_ERR_UNLIKELY;
}

static int on_color_scv_access_format(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                      void *arg) {
  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_DSC) {
    ESP_LOGW(TAG, "Unsupported op %d for on_color_scv_access_format", ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
  }

  static const uint8_t cpf_uint8[7] = {
      0x04,       /* uint8 */
      0x00,       /* exponent */
      0x00, 0x27, /* unit: unitless (LE) */
      0x01,       /* namespace: BT SIG */
      0x00, 0x00, /* description (LE) */
  };
  const int ret = os_mbuf_append(ctxt->om, cpf_uint8, sizeof(cpf_uint8));
  return (ret == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int on_color_scv_access_val(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                                   void *arg) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    if (strcmp(arg, "Red") == 0) {
      g_color_r = ctxt->om->om_data[0];
      os_mbuf_append(ctxt->om, &g_color_r, sizeof(g_color_r));
    } else if (strcmp(arg, "Green") == 0) {
      os_mbuf_append(ctxt->om, &g_color_g, sizeof(g_color_g));
    } else if (strcmp(arg, "Blue") == 0) {
      os_mbuf_append(ctxt->om, &g_color_b, sizeof(g_color_b));
    } else {
      ESP_LOGE(TAG, "Invalid op arg for on_color_scv_access_val");
      return BLE_ATT_ERR_UNLIKELY;
    }
    return 0;
  }

  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    if (ctxt->om->om_len < 1) {
      ESP_LOGE(TAG, "Invalid op args len on_color_scv_access_val");
      return BLE_ATT_ERR_UNLIKELY;
    }

    if (strcmp(arg, "Red") == 0) {
      g_color_r = ctxt->om->om_data[0];
    } else if (strcmp(arg, "Green") == 0) {
      g_color_g = ctxt->om->om_data[0];
    } else if (strcmp(arg, "Blue") == 0) {
      g_color_b = ctxt->om->om_data[0];
    } else {
      ESP_LOGE(TAG, "Invalid op arg for on_color_scv_access_val");
      return BLE_ATT_ERR_UNLIKELY;
    }
    ESP_LOGI(TAG, "Update R=%d G=%d B=%d", g_color_r, g_color_g, g_color_b);
    if (g_led_on) {
      c3_zero_led_set(g_color_r, g_color_g, g_color_b);
    }
    return 0;
  }

  ESP_LOGE(TAG, "Unsupported op %d for on_color_scv_access_val", ctxt->op);
  return BLE_ATT_ERR_UNLIKELY;

  return 0;
}
