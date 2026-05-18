#pragma once
#include <stdbool.h>

// Config reset handler (nukes the NVS)
//
// Usage: set up as a handler together with btn_mon. You can still use the button as normal.
// Whenever a button is pressed, this callback does nothing (so you can still use the button for other function).
// When a button is held pressed (invoked with active=true, active=false not invoked), a timer for N seconds will be configured. After N seconds, an LED will flash red for a
// second stage confirmation. If the user confirms (by pressing) the same button again while the LED flashes red, the
// NVS will be reset. If no confirmation is received, the reset process is cancelled. The reset process is also
// cancelled if the button is held pressed, to avoid accidental resets (eg if the button is held active due to a fault).
//
// After a config reset is complete, the device will reboot.
//
// LEDs:
// - Quickly flashing red: waiting for user confirmation before reset
// - Breathing effect red: reset confirmed
// - Solid green: reset complete
void provision_maybe_reset(bool active);
