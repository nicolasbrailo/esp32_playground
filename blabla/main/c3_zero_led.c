#include "c3_zero_led.h"

#include "esp_log.h"
#include "led_strip.h"

#define GPIO_LED_PIN 10

static const char *TAG = "LED";
static led_strip_handle_t g_led_strip;

void c3_zero_led_init() {
  ESP_LOGI(TAG, "Configure LED strip");
  led_strip_config_t strip_config = {
      .strip_gpio_num = GPIO_LED_PIN,
      .max_leds = 1,
      .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB, // Without this may default to GRB
  };
  led_strip_rmt_config_t rmt_config = {
      .resolution_hz = 10 * 1000 * 1000, // 10MHz
      .flags.with_dma = false,
  };

  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &g_led_strip));
}

void c3_zero_led_set(uint8_t r, uint8_t g, uint8_t b) {
  /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
  led_strip_set_pixel(g_led_strip, 0, r, g, b);
  /* Refresh the strip to send data */
  led_strip_refresh(g_led_strip);
}

void c3_zero_led_clear() { led_strip_clear(g_led_strip); }
