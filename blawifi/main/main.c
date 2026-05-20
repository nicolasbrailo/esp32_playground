#include "common/btn_mon.h"
#include "common/c3_zero_led.h"
#include "mqtt.h"
#include "provisioning/reset.h"
#include "provisioning/wifi.h"
#include "wifi.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"

#define TAG "blawifi"

// Battery .h
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

// In ESP32 C3 zero, the boot button is wired to GPIO 9
#define BTN_GPIO 9
// PIR sensor output. Pick a safe GPIO (avoid 0/2/8/9/18/19/20/21).
#define PIR_GPIO 1

#define BATT_ADC_CHANNEL ADC_CHANNEL_2 // GPIO2
#define BATT_ADC_ATTEN ADC_ATTEN_DB_12 // ~0-3.1V input range

// PIR wiring:
//   GPIO8  = VCC  (strapping pin requires HIGH at boot — matches our usage)
//   GPIO6  = signal (general-purpose, no peripheral conflicts)
//   GPIO10 = GND  (no strapping role, safe to drive LOW)
#define PIR_VCC_GPIO 8
#define PIR_GPIO 6
#define PIR_GND_GPIO 10

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;

static void battery_init(void) {
  adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1};
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

  adc_oneshot_chan_cfg_t chan_cfg = {
      .atten = BATT_ADC_ATTEN,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BATT_ADC_CHANNEL, &chan_cfg));

  adc_cali_curve_fitting_config_t cali_cfg = {
      .unit_id = ADC_UNIT_1,
      .atten = BATT_ADC_ATTEN,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali));
}

static void pir_power_init(void) {
  const gpio_config_t cfg = {
      .pin_bit_mask = (1ULL << PIR_VCC_GPIO) | (1ULL << PIR_GND_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&cfg));
  // Drive strength levels: CAP_0 ~5mA, CAP_1 ~10mA, CAP_2 ~20mA (default), CAP_3 ~40mA.
  // CAP_3 keeps the rail close to 3.3V when used as VCC for a sensor.
  gpio_set_drive_capability(PIR_VCC_GPIO, GPIO_DRIVE_CAP_3);
  gpio_set_level(PIR_GND_GPIO, 0);
  gpio_set_level(PIR_VCC_GPIO, 1);
  vTaskDelay(pdMS_TO_TICKS(20));
}


// Returns battery voltage in millivolts (3000-4200 typical for LiPo).
// Averages 16 samples to smooth ADC noise from the 50k source impedance.
static int battery_millivolts(void) {
  int sum = 0;
  for (int i = 0; i < 16; i++) {
    int raw;
    ESP_ERROR_CHECK(adc_oneshot_read(s_adc, BATT_ADC_CHANNEL, &raw));
    sum += raw;
  }
  int mv;
  ESP_ERROR_CHECK(adc_cali_raw_to_voltage(s_cali, sum / 16, &mv));
  return mv * 2; // undo the 100k/100k divider
}

static void battery_task(void *arg) {
  while (true) {
    int mv = battery_millivolts();
    ESP_LOGI(TAG, "Battery: %d mV", mv);
    mqtt_report_battery(mv);
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

static struct provisioning_config g_cfg;
static bool g_cfg_ready = false;

static void on_wifi_up(esp_ip4_addr_t ip) {
  (void)ip;
  ESP_LOGI(TAG, "Connectivity up, connect to MQTT");
  configASSERT(g_cfg_ready);
  ESP_ERROR_CHECK(mqtt_init(g_cfg.mqtt_url, g_cfg.mqtt_url, g_cfg.mqtt_pwd, g_cfg.this_device_name));
  ESP_ERROR_CHECK(mqtt_start());
  c3_zero_led_blink(/*n=*/1, /*on_ms=*/200, /*off_ms=*/100, /*r=*/0, /*g=*/200, /*b=*/0);
}

static void on_wifi_down(void) {
  ESP_LOGI(TAG, "Lost connectivity");
  c3_zero_led_blink(/*n=*/1, /*on_ms=*/200, /*off_ms=*/100, /*r=*/200, /*g=*/0, /*b=*/0);
}

static void on_provisioning_complete(const struct provisioning_config *cfg) {
  g_cfg = *cfg;
#if 1
  strcpy(g_cfg.mqtt_url, "mqtt://10.0.0.10:4500");
#endif
  g_cfg_ready = true;

  ESP_LOGI(TAG, "Provisioning received:");
  ESP_LOGI(TAG, "  this_device_name = %s", g_cfg.this_device_name);
  ESP_LOGI(TAG, "  ap_name          = %s", g_cfg.ap_name);
  ESP_LOGI(TAG, "  ap_pwd           = %s", g_cfg.ap_pwd);
  ESP_LOGI(TAG, "  mqtt_url         = %s", g_cfg.mqtt_url);
  ESP_LOGI(TAG, "  mqtt_usr         = %s", g_cfg.mqtt_usr);
  ESP_LOGI(TAG, "  mqtt_pwd         = %s", g_cfg.mqtt_pwd);

  struct wifi_cbs cbs = {.on_wifi_up = on_wifi_up, .on_wifi_down = on_wifi_down};
  const esp_err_t rc = wifi_connect(g_cfg.ap_name, g_cfg.ap_pwd, cbs);
  if (rc != ESP_OK) {
    ESP_LOGE(TAG, "wifi_connect failed: %s", esp_err_to_name(rc));
  }
}

static void on_boot_btn(bool active, void *usr) {
  (void)usr;
  provision_maybe_reset_btn_handler(active);
  c3_zero_led_blink(/*n=*/4, /*on_ms=*/200, /*off_ms=*/100, /*r=*/50, /*g=*/80, /*b=*/0);
}

static void on_pir(bool active, void *usr) {
  ESP_LOGI(TAG, "PIR reports %sactive", active? "" : "in");
  mqtt_report_presence(active);
}

void app_main(void);
void app_main(void) {
  // Quiet the IDF wifi driver — keeps WARN/ERROR, drops the verbose state-machine
  // chatter (channel transitions, beacon timing, etc).
  esp_log_level_set("wifi", ESP_LOG_WARN);
  esp_log_level_set("wifi_init", ESP_LOG_WARN);

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
      {
          .gpio = PIR_GPIO,
          .active_high = true,
          .pull = BTN_MON_PULL_NONE,
          .callback = &on_pir,
      },
  };
  ESP_ERROR_CHECK(btn_mon_init(handlers, sizeof(handlers) / sizeof(handlers[0]), NULL));
  ESP_ERROR_CHECK(wifi_provision_init(&on_provisioning_complete));

  battery_init();
  pir_power_init();
  xTaskCreate(battery_task, "batt", 4096, NULL, 5, NULL);
}
