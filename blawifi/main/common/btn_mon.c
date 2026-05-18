#include "common/btn_mon.h"

#include "driver/gpio.h"
#include "esp_intr_alloc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"
static const char *TAG = "btn-mon";

#define BTN_DEBOUNCE_US (50 * 1000) // 50 ms

static QueueHandle_t g_gpio_evt_queue;
static void *g_usr_arg;
static struct btn_mon_hanlder *g_hdlrs;
static size_t g_hdls_sz;
static int64_t *g_last_fire_us;

static void IRAM_ATTR gpio_isr_handler(void *arg) {
  uint32_t hdlr_id = (uint32_t)arg;
  xQueueSendFromISR(g_gpio_evt_queue, &hdlr_id, NULL);
}

static void button_check_task(void *arg) {
  for (;;) {
    uint32_t hdlr_id;
    if (xQueueReceive(g_gpio_evt_queue, &hdlr_id, portMAX_DELAY)) {
      if (hdlr_id < g_hdls_sz) {
        const int64_t now = esp_timer_get_time();
        if (now - g_last_fire_us[hdlr_id] < BTN_DEBOUNCE_US) {
          continue;
        }
        g_last_fire_us[hdlr_id] = now;
        const int level = gpio_get_level(g_hdlrs[hdlr_id].gpio);
        const bool active = g_hdlrs[hdlr_id].active_high ? level : !level;
        ESP_LOGD(TAG, "GPIO %d is %s", g_hdlrs[hdlr_id].gpio, active ? "active" : "inactive");
        g_hdlrs[hdlr_id].callback(active, g_usr_arg);
      } else {
        ESP_LOGE(TAG, "ISR reported for GPIO id %d, over max of %d. This shouldn't happen.", hdlr_id, g_hdls_sz);
      }
    }
  }
}

esp_err_t btn_mon_init(const struct btn_mon_hanlder *hdls, size_t hdls_sz, void *usr_arg) {
  if (g_gpio_evt_queue != NULL) {
    ESP_LOGE(TAG, "Tried to initialize btn_mon twice");
    return ESP_ERR_INVALID_STATE;
  }

  g_gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
  if (g_gpio_evt_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create ISR event queue");
    return ESP_ERR_INVALID_STATE;
  }

  g_usr_arg = usr_arg;
  g_hdls_sz = hdls_sz;
  g_hdlrs = malloc(hdls_sz * sizeof(struct btn_mon_hanlder));
  if (g_hdlrs == NULL)
    return ESP_FAIL;
  memcpy(g_hdlrs, hdls, hdls_sz * sizeof(struct btn_mon_hanlder));

  g_last_fire_us = calloc(hdls_sz, sizeof(int64_t));
  if (g_last_fire_us == NULL)
    return ESP_FAIL;

  ESP_RETURN_ON_ERROR(gpio_install_isr_service(ESP_INTR_FLAG_IRAM), TAG, "Failed to setup ISR");
  for (size_t i = 0; i < hdls_sz; ++i) {
    const gpio_config_t io_conf = {
        // interrupt anyedge will trigger on press and release. NEGEDGE will be press only, POSEDGE release.
        .intr_type = GPIO_INTR_ANYEDGE,
        .pin_bit_mask = (1ULL << hdls[i].gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (hdls[i].pull == BTN_MON_PULL_UP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (hdls[i].pull == BTN_MON_PULL_DOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Failed to setup GPIO %d", hdls[i].gpio);
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(hdls[i].gpio, gpio_isr_handler, (void *)i), TAG,
                        "Failed to setup GPIO %d ISR", hdls[i].gpio);
    ESP_LOGI(TAG, "will monitor GPIO %d", hdls[i].gpio);
  }

  // start handler task last, so we don't risk handling an interrupt while setting up state
  if (xTaskCreate(button_check_task, "button_check_task", 2 * 1024, NULL, 3, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Failed to start button check task");
    return ESP_FAIL;
  }

  return ESP_OK;
}
