#include "btn_mon.h"

#include "esp_log.h"
static const char *TAG = "gpio-pir";

// In ESP32 C3 zero, the boot button is wired to GPIO 9
#define BTN_GPIO 9
// PIR sensor output. Pick a safe GPIO (avoid 0/2/8/9/18/19/20/21).
#define PIR_GPIO 1

void on_boot_btn(bool active, void *) {
  if (active)
    ESP_LOGI(TAG, "Button pressed");
}

void on_pir(bool active, void *) { ESP_LOGI(TAG, "PIR: motion %s", active ? "detected" : "ended"); }

void app_main() {
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
}
