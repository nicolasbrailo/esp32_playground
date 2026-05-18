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
// The LED will breath white while provisioning mode is active
esp_err_t wifi_provision_init(on_provisioning_complete_t cb);

