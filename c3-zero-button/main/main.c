#include "btn_mon.h"

#include "esp_log.h"
static const char *TAG = "c3-zero-button";

// In ESP32 C3 zero, the boot button is wired to GPIO 9
#define BTN_GPIO 9

void on_boot_btn(bool pressed, void *) {
  if (pressed)
    ESP_LOGI(TAG, "HOLA!");
}

void app_main() {
  const struct btn_mon_hanlder btns[] = {{
      .gpio = BTN_GPIO,
      .callback = &on_boot_btn,
  }};

  ESP_ERROR_CHECK(btn_mon_init(btns, sizeof(btns) / sizeof(btns[0]), NULL));
}
