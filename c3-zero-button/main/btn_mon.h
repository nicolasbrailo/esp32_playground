#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

struct btn_mon_hanlder {
  uint8_t gpio;
  void (*callback)(bool pressed, void *arg);
};

esp_err_t btn_mon_init(const struct btn_mon_hanlder *hdls, size_t hdls_sz, void *usr_arg);
