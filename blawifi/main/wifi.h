#pragma once

#include "esp_err.h"
#include "esp_wifi.h"

struct wifi_cbs {
  void (*on_wifi_up)(esp_ip4_addr_t ip);
  void (*on_wifi_down)();
};

// Initializes wifi in client mode using cfg.ap_name / cfg.ap_pwd and starts the
// connection. On disconnect the driver retries indefinitely with a warning log
// — wrong credentials will spin forever. Returns once the start is initiated;
// the actual connection completes asynchronously.
esp_err_t wifi_connect(const char *ap_name, const char *ap_pwd, struct wifi_cbs cbs);
