#include "custom_panic.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_private/esp_clk.h"
#include "esp_rom_sys.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

#define GPIO_LED_PIN 10

// WS2812 bit timing
#define WS2812_T0H_NS 350
#define WS2812_T0L_NS 900
#define WS2812_T1H_NS 900
#define WS2812_T1L_NS 350
#define WS2812_RESET_US 80

static uint32_t g_cyc_t0h, g_cyc_t0l, g_cyc_t1h, g_cyc_t1l;

static inline void delay_cycles(uint32_t cycles) {
  uint32_t start = esp_cpu_get_cycle_count();
  while ((esp_cpu_get_cycle_count() - start) < cycles) {
  }
}

static inline void ws2812_send_byte(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    if ((b >> i) & 1) {
      REG_WRITE(GPIO_OUT_W1TS_REG, 1U << GPIO_LED_PIN);
      delay_cycles(g_cyc_t1h);
      REG_WRITE(GPIO_OUT_W1TC_REG, 1U << GPIO_LED_PIN);
      delay_cycles(g_cyc_t1l);
    } else {
      REG_WRITE(GPIO_OUT_W1TS_REG, 1U << GPIO_LED_PIN);
      delay_cycles(g_cyc_t0h);
      REG_WRITE(GPIO_OUT_W1TC_REG, 1U << GPIO_LED_PIN);
      delay_cycles(g_cyc_t0l);
    }
  }
}

static void ws2812_send_rgb(uint8_t r, uint8_t g, uint8_t b) {
  // WS2812 wire order is GRB, MSB first
  ws2812_send_byte(g);
  ws2812_send_byte(r);
  ws2812_send_byte(b);
  esp_rom_delay_us(WS2812_RESET_US);
}

void custom_panic(const char *msg) {
  ESP_LOGE("PANIC!!", "%s", msg);

  // Set up GPIO and pre-compute timing while normal APIs are still usable
  gpio_reset_pin(GPIO_LED_PIN);
  gpio_set_direction(GPIO_LED_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_LED_PIN, 0);

  uint32_t cpu_hz = esp_clk_cpu_freq();
  g_cyc_t0h = (uint64_t)WS2812_T0H_NS * cpu_hz / 1000000000ULL;
  g_cyc_t0l = (uint64_t)WS2812_T0L_NS * cpu_hz / 1000000000ULL;
  g_cyc_t1h = (uint64_t)WS2812_T1H_NS * cpu_hz / 1000000000ULL;
  g_cyc_t1l = (uint64_t)WS2812_T1L_NS * cpu_hz / 1000000000ULL;

  // Don't fully halt: the interrupt and task watchdogs would trip if interrupts
  // stayed disabled or the scheduler stopped. Instead, only block interrupts for
  // the ~30 us of each WS2812 frame (to protect bit timing) and yield with
  // vTaskDelay between frames so the watchdogs get serviced. This task never
  // returns, so the calling code is effectively dead.
  while (1) {
    portDISABLE_INTERRUPTS();
    ws2812_send_rgb(255, 0, 0);
    portENABLE_INTERRUPTS();
    vTaskDelay(pdMS_TO_TICKS(1000));

    portDISABLE_INTERRUPTS();
    ws2812_send_rgb(0, 255, 0);
    portENABLE_INTERRUPTS();
    vTaskDelay(pdMS_TO_TICKS(1000));

    portDISABLE_INTERRUPTS();
    ws2812_send_rgb(0, 0, 255);
    portENABLE_INTERRUPTS();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
