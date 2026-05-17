#include "wifi_provision.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define TAG "blawifi"

void on_provisioning_complete(const struct provisioning_config *cfg) {
  ESP_LOGI(TAG, "Provisioning received:");
  ESP_LOGI(TAG, "  this_device_name = %s", cfg->this_device_name);
  ESP_LOGI(TAG, "  ap_name          = %s", cfg->ap_name);
  ESP_LOGI(TAG, "  ap_pwd           = %s", cfg->ap_pwd);
  ESP_LOGI(TAG, "  mqtt_url         = %s", cfg->mqtt_url);
  ESP_LOGI(TAG, "  mqtt_usr         = %s", cfg->mqtt_usr);
  ESP_LOGI(TAG, "  mqtt_pwd         = %s", cfg->mqtt_pwd);
}

void app_main() {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(wifi_provision_init(&on_provisioning_complete));

  while (true) {
    ESP_LOGI(TAG, "HOLA");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
