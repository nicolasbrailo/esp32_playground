#pragma once

struct ble_gatt_svc_def;

/* GATT table for a BLE HID over GATT Profile (HOGP) "Consumer Control" device.
 * The device intentionally never sends any HID reports — its purpose is solely
 * to be a recognised HID peripheral so the phone's OS auto-reconnects when in
 * range. Connection state then serves as a presence signal. */
const struct ble_gatt_svc_def *bt_hid_get_gatt_def();
