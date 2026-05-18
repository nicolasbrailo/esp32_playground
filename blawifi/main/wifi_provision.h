#pragma once

#include "esp_err.h"

struct provisioning_config {
  char this_device_name[64];
  char ap_name[64];
  char ap_pwd[64];
  char mqtt_url[64];
  char mqtt_usr[64];
  char mqtt_pwd[64];
};

typedef void (*on_provisioning_complete_t)(const struct provisioning_config *cfg);

// Creates a dummy access point. A client can connect to it, set up initial params, and the callback will be
// invoked after the users finishes configuration. When the callback is invoked, the dummy AP is already down.
esp_err_t wifi_provision_init(on_provisioning_complete_t cb);

// Erases any saved provisioning config from NVS so the next wifi_provision_init
// call starts the captive portal again. Returns ESP_OK if no config was saved.
esp_err_t wifi_provision_clear(void);
