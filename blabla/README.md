# blabla

BT device example for ESP32 C3 Zero. Exposes the internal RGB LED over USB.
Use nRF Connect to explore the exposed BT interface.

BLE configured to act as a presence monitor: should flash blue when connected, then automatically disconnect (and flash red) if you move 3/4 meters away from the ESP.

