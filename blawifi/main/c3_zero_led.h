#pragma once

#include <stdint.h>
#include <stddef.h>

void c3_zero_led_init();
void c3_zero_led_set(uint8_t r, uint8_t g, uint8_t b);
void c3_zero_led_blink(uint8_t n, size_t on_ms, size_t off_ms, uint8_t r, uint8_t g, uint8_t b);
void c3_zero_led_clear();
