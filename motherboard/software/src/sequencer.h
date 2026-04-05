#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MIDI_BUFFER_SIZE 256

typedef struct {
	uint8_t buffer_A[MIDI_BUFFER_SIZE];
	uint8_t buffer_B[MIDI_BUFFER_SIZE];
	uint16_t length_A;
	uint16_t length_B;
	bool fill_buffer_A_next;
	volatile bool is_busy;
} MidiPort;

extern MidiPort midi_out_1;
extern MidiPort midi_out_2;

extern bool stop_seq_request;

extern uint8_t current_tempo;

extern uint16_t currect_chain_track_selector;

void midi_out_init(void);
void midi_send(MidiPort *port);
bool event_start_seq();
