#ifndef MIDI_DRIVER_H
#define MIDI_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#define MIDI_BUFFER_SIZE 256

void midi_driver_init(void);
void midi_driver_send(uint8_t port_idx, const uint8_t* data, uint16_t length);
bool midi_driver_is_busy(uint8_t port_idx);

#endif
