#include "provision_reset.h"

#include "c3_zero_led.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_system.h"
#include <stdatomic.h>

#include "esp_log.h"
#define TAG "blawifi"

// How long the boot button must be held before config reset triggers.
#define CFG_RESET_HOLD_STEP1_US (3 * 1000 * 1000)
// Step1: if a second button press is received in this window, we trigger reset. If button
// is held all the time or no second press arrives, we stop reset process.
#define CFG_RESET_STEP1_WINDOW_MS (3 * 1000)
#define CFG_STEP1_LED_DUTY_CYCLE_MS (50)

static esp_timer_handle_t g_cfg_reset_timer = NULL;
static atomic_bool g_step1_engaged = false;

static void on_config_reset_step1(void *arg) {
  ESP_LOGI(TAG, "Config reset step 1 engaged");
  atomic_store_explicit(&g_step1_engaged, true, memory_order_relaxed);
  size_t cnt = CFG_RESET_STEP1_WINDOW_MS / (2 * CFG_STEP1_LED_DUTY_CYCLE_MS);
  while (cnt-- > 0) {
    c3_zero_led_blink(/*n=*/1, /*on_ms=*/50, /*off_ms=*/50, /*r=*/50, /*g=*/0, /*b=*/0);
    if (!atomic_load_explicit(&g_step1_engaged, memory_order_relaxed)) {
      break;
    }
  }

  if (atomic_load_explicit(&g_step1_engaged, memory_order_relaxed)) {
    // Reached the end of the loops, no second press detected
    atomic_store_explicit(&g_step1_engaged, false, memory_order_relaxed);
    ESP_LOGI(TAG, "Config reset step 1 disengaged");
  } else {
    // We break off the loop because user confirmed reset, the main handler will take care of it
  }
}

static void signal_reset_ack() {
  // Flash red to signal reset will proceed
  for (size_t i = 0; i < 5; ++i) {
    c3_zero_led_set(10, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    c3_zero_led_set(255, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

static void signal_reset_ok() {
  // solid green to confirm reset complete
  c3_zero_led_set(0, 100, 0);
  vTaskDelay(pdMS_TO_TICKS(500));
  c3_zero_led_clear();
}

static esp_err_t nuke_nvs() {
  signal_reset_ack();
  // Wipes the entire default NVS partition — every namespace, every key.
  // Partition stays unusable until nvs_flash_init() runs again, which is fine
  // here because we reboot immediately after.
  // If this fails, no idea what will happen
  ESP_ERROR_CHECK(nvs_flash_erase());
  signal_reset_ok();
  ESP_LOGI(TAG, "Rebooting to re-enter provisioning");
  esp_restart(); // does not return
}

void provision_maybe_reset(bool active) {
  if (g_cfg_reset_timer == NULL) {
    const esp_timer_create_args_t cfg_reset_args = {
        .callback = &on_config_reset_step1,
        .name = "cfg_reset",
    };
    ESP_ERROR_CHECK(esp_timer_create(&cfg_reset_args, &g_cfg_reset_timer));
  }

  // Hold-to-reset: start a timer on press; release before it fires cancels.
  // esp_timer_stop returns INVALID_STATE if not running — harmless, ignore.
  esp_timer_stop(g_cfg_reset_timer);

  if (active && atomic_load_explicit(&g_step1_engaged, memory_order_relaxed)) {
    ESP_LOGI(TAG, "Config reset confirmed, will reset NVMS");
    atomic_store_explicit(&g_step1_engaged, false, memory_order_relaxed);
    nuke_nvs();
  }

  if (active) {
    ESP_ERROR_CHECK(esp_timer_start_once(g_cfg_reset_timer, CFG_RESET_HOLD_STEP1_US));
  }
}
