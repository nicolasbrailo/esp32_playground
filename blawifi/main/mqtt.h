#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t mqtt_init(const char *mqtt_url, const char *mqtt_usr, const char *mqtt_pwd, const char *topic);

esp_err_t mqtt_start();

void mqtt_report_presence(bool presence);
void mqtt_report_battery(int batt_mv);
