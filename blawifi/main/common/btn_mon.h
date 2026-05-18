#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

enum btn_mon_pull {
  BTN_MON_PULL_NONE, // Floating — for sensors with their own push-pull driver (e.g. PIR).
  BTN_MON_PULL_UP,   // Internal ~45 kΩ pull-up — for buttons shorting to GND.
  BTN_MON_PULL_DOWN, // Internal ~45 kΩ pull-down — for buttons shorting to VCC.
};

struct btn_mon_hanlder {
  uint8_t gpio;
  // true if a high level on the pin means "active" (e.g. PIR output).
  // false if a low level means "active" (e.g. button shorting to GND with a pull-up).
  bool active_high;
  enum btn_mon_pull pull;
  void (*callback)(bool active, void *arg);
};

// Install a set of callback handlers. No cleanup possible. Callbacks are invoked from a task, not from an interrupt
// context.
esp_err_t btn_mon_init(const struct btn_mon_hanlder *hdls, size_t hdls_sz, void *usr_arg);
