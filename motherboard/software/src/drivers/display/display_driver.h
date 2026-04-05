#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

void display_driver_init(void);
void display_driver_set_segments(uint8_t left_mask, uint8_t right_mask);
void display_driver_set_delimiters(bool upper, bool lower);
void display_driver_clear(void);
void display_driver_next_digit(void);

#endif
