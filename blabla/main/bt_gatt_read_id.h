#pragma once

#include <stdint.h>

typedef void (*bt_name_read_cb)(uint16_t conn_handle, const char *name, size_t name_len,
                                void *user_arg);

/* Asynchronously read the peer's Generic Access "Device Name" characteristic
 * (GAP service 0x1800, characteristic 0x2A00). The user callback is always
 * invoked exactly once — with the name on success, or with name=NULL/len=0
 * on any failure (no service, no characteristic, read error, disconnect).
 */
int bt_gatt_read_device_name(uint16_t conn_handle, bt_name_read_cb cb, void *user_arg);
