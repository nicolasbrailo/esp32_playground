#include "common/btn_mon.h"
#include "common/c3_zero_led.h"
#include "provisioning/reset.h"
#include "provisioning/wifi.h"
#include "wifi.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define TAG "blawifi"

// In ESP32 C3 zero, the boot button is wired to GPIO 9
#define BTN_GPIO 9

static void on_wifi_up(esp_ip4_addr_t ip) { ESP_LOGI(TAG, "CONN UP"); }

static void on_wifi_down() { ESP_LOGI(TAG, "Lost connectivity"); }

static void on_provisioning_complete(const struct provisioning_config *cfg) {
  ESP_LOGI(TAG, "Provisioning received:");
  ESP_LOGI(TAG, "  this_device_name = %s", cfg->this_device_name);
  ESP_LOGI(TAG, "  ap_name          = %s", cfg->ap_name);
  ESP_LOGI(TAG, "  ap_pwd           = %s", cfg->ap_pwd);
  ESP_LOGI(TAG, "  mqtt_url         = %s", cfg->mqtt_url);
  ESP_LOGI(TAG, "  mqtt_usr         = %s", cfg->mqtt_usr);
  ESP_LOGI(TAG, "  mqtt_pwd         = %s", cfg->mqtt_pwd);

  struct wifi_cbs cbs = {.on_wifi_up = on_wifi_up, .on_wifi_down = on_wifi_down};
  const esp_err_t rc = wifi_connect(cfg->ap_name, cfg->ap_pwd, cbs);
  if (rc != ESP_OK) {
    ESP_LOGE(TAG, "wifi_connect failed: %s", esp_err_to_name(rc));
  }
}

static void on_boot_btn(bool active, void *usr) {
  provision_maybe_reset_btn_handler(active);
  c3_zero_led_blink(/*n=*/4, /*on_ms=*/200, /*off_ms=*/100, /*r=*/50, /*g=*/80, /*b=*/0);
}

void app_main();
void app_main() {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Start up: quick white flash to indicate we're starting main
  ESP_ERROR_CHECK(c3_zero_led_init());
  c3_zero_led_blink(/*n=*/1, /*on_ms=*/100, /*off_ms=*/100, /*r=*/50, /*g=*/50, /*b=*/50);

  const struct btn_mon_hanlder handlers[] = {
      {
          .gpio = BTN_GPIO,
          .active_high = false,
          .pull = BTN_MON_PULL_UP,
          .callback = &on_boot_btn,
      },
  };
  ESP_ERROR_CHECK(btn_mon_init(handlers, sizeof(handlers) / sizeof(handlers[0]), NULL));
  ESP_ERROR_CHECK(wifi_provision_init(&on_provisioning_complete));
}
