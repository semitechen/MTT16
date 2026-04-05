#include "sequencer.h"
#include "../../drivers/midi/midi_driver.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

#define USEC_PER_MINUTE 60000000
#define STEPS_PER_BEAT 4
#define MICRO_DELAY_MAX 256
#define MAX_EVENTS_PER_STEP 256
#define WAIT_POLL_MS 10
#define MAX_LOAD_RETRIES 10

#define MIDI_TYPE_MASK 0xF0
#define MIDI_PROG_CHANGE 0xC0
#define MIDI_CHAN_PRESSURE 0xD0
#define MIDI_REALTIME_MASK 0xF8

#define MIDI_CHANNELS 16
#define MIDI_NOTE_CHUNKS 4
#define BITS_PER_CHUNK 32
#define MIDI_CHANNEL_MASK 0x0F
#define MIDI_NOTE_OFF 0x80
#define MIDI_NOTE_ON 0x90

typedef struct {
	uint8_t buffer_A[MIDI_BUFFER_SIZE];
	uint8_t buffer_B[MIDI_BUFFER_SIZE];
	uint16_t length_A;
	uint16_t length_B;
	bool fill_buffer_A_next;
} MidiBuffer;

static MidiBuffer out_buffers[2];
static uint8_t current_tempo = 120;
static bool stop_seq_request = false;
static bool is_playing = false;
static uint64_t current_chain_track_selector = 0;
static Chain Seq_chains[NUM_OF_CHAINS];
static SongProvider song_provider_cb = NULL;

void sequencer_init(SongProvider provider) {
	song_provider_cb = provider;
	midi_driver_init();
	for (int i = 0; i < 2; i++) {
		out_buffers[i].fill_buffer_A_next = true;
		out_buffers[i].length_A = 0;
		out_buffers[i].length_B = 0;
	}
}

static uint8_t get_midi_msg_len(uint8_t status) {
	if (status >= MIDI_REALTIME_MASK) return 1;
	uint8_t type = status & MIDI_TYPE_MASK;
	if (type == MIDI_PROG_CHANGE || type == MIDI_CHAN_PRESSURE) return 2;
	return 3;
}

static void push_to_port(uint8_t port_idx, MidiEvent* ev) {
	MidiBuffer* b = &out_buffers[port_idx];
	uint8_t len = get_midi_msg_len(ev->status);
	uint16_t* cur_len = b->fill_buffer_A_next ? &b->length_A : &b->length_B;
	uint8_t* buf = b->fill_buffer_A_next ? b->buffer_A : b->buffer_B;

	if (*cur_len + len > MIDI_BUFFER_SIZE) {
		midi_driver_send(port_idx, buf, *cur_len);
		b->fill_buffer_A_next = !b->fill_buffer_A_next;
		cur_len = b->fill_buffer_A_next ? &b->length_A : &b->length_B;
		buf = b->fill_buffer_A_next ? b->buffer_A : b->buffer_B;
		*cur_len = 0;
	}

	buf[*cur_len] = ev->status;
	if (len > 1) buf[*cur_len + 1] = ev->data1;
	if (len > 2) buf[*cur_len + 2] = ev->data2;
	*cur_len += len;
}

static void flush_port(uint8_t port_idx) {
	MidiBuffer* b = &out_buffers[port_idx];
	uint16_t* cur_len = b->fill_buffer_A_next ? &b->length_A : &b->length_B;
	uint8_t* buf = b->fill_buffer_A_next ? b->buffer_A : b->buffer_B;
	if (*cur_len > 0) {
		midi_driver_send(port_idx, buf, *cur_len);
		b->fill_buffer_A_next = !b->fill_buffer_A_next;
		cur_len = b->fill_buffer_A_next ? &b->length_A : &b->length_B;
		*cur_len = 0;
	}
}

static bool play_songs(Song* song_a, Song* song_b, uint16_t selector) {
	if (!song_a || !song_b) return false;
	uint32_t active_notes[MIDI_CHANNELS][MIDI_NOTE_CHUNKS] = {0};
	uint32_t step_us = USEC_PER_MINUTE / (current_tempo * STEPS_PER_BEAT);
	uint32_t track_indices[MAX_MUSIC_TRACKS] = {0};
	uint32_t current_step = 0;

	absolute_time_t next_step_time = get_absolute_time();

	while (!stop_seq_request) {
		MidiEvent* step_events[MAX_EVENTS_PER_STEP];
		int step_event_count = 0;
		bool any_track_active = false;

		for (int t = 0; t < MAX_MUSIC_TRACKS; t++) {
			Song* active_song = (selector & (1 << (MAX_MUSIC_TRACKS - t - 1))) ? song_b : song_a;
			Track* trk = &active_song->tracks[t];
			if (track_indices[t] < trk->event_count) {
				any_track_active = true;
				while (track_indices[t] < trk->event_count && trk->events[track_indices[t]].step == current_step) {
					if (step_event_count < MAX_EVENTS_PER_STEP) {
						step_events[step_event_count++] = &trk->events[track_indices[t]];
					}
					track_indices[t]++;
				}
			}
		}

		if (!any_track_active && step_event_count == 0) break;

		for (int i = 1; i < step_event_count; i++) {
			MidiEvent* key = step_events[i];
			int j = i - 1;
			while (j >= 0 && step_events[j]->micro_delay > key->micro_delay) {
				step_events[j+1] = step_events[j];
				j--;
			}
			step_events[j+1] = key;
		}

		int i = 0;
		while (i < step_event_count) {
			uint8_t cur_delay = step_events[i]->micro_delay;
			absolute_time_t ev_time = delayed_by_us(next_step_time, (step_us * cur_delay) / MICRO_DELAY_MAX);
			sleep_until(ev_time);
			while (i < step_event_count && step_events[i]->micro_delay == cur_delay) {
				uint8_t status = step_events[i]->status;
				uint8_t type = status & MIDI_TYPE_MASK;
				uint8_t ch = status & MIDI_CHANNEL_MASK;
				uint8_t note = step_events[i]->data1;
				if (type == MIDI_NOTE_ON) active_notes[ch][note / BITS_PER_CHUNK] |= (1UL << (note % BITS_PER_CHUNK));
				else if (type == MIDI_NOTE_OFF) active_notes[ch][note / BITS_PER_CHUNK] &= ~(1UL << (note % BITS_PER_CHUNK));
				push_to_port(0, step_events[i]);
				push_to_port(1, step_events[i]);
				i++;
			}
			flush_port(0);
			flush_port(1);
		}
		next_step_time = delayed_by_us(next_step_time, step_us);
		sleep_until(next_step_time);
		current_step++;
	}

	for (int ch = 0; ch < MIDI_CHANNELS; ch++) {
		for (int chunk = 0; chunk < MIDI_NOTE_CHUNKS; chunk++) {
			uint32_t mask = active_notes[ch][chunk];
			for (int bit = 0; bit < BITS_PER_CHUNK && mask; bit++) {
				if (mask & (1UL << bit)) {
					MidiEvent note_off = {0, 0, MIDI_NOTE_OFF | ch, (chunk * BITS_PER_CHUNK) + bit, 0};
					push_to_port(0, &note_off);
					push_to_port(1, &note_off);
					mask &= ~(1UL << bit);
				}
			}
		}
	}
	flush_port(0);
	flush_port(1);
	return true;
}

void sequencer_start(void) {
	if (!song_provider_cb) return;
	is_playing = true;
	stop_seq_request = false;
	uint64_t chain_step_counter = 0;
	while (!stop_seq_request) {
		uint8_t song_id_a = Seq_chains[CHAIN_A].songs[chain_step_counter % Seq_chains[CHAIN_A].length];
		uint8_t song_id_b = Seq_chains[CHAIN_B].songs[chain_step_counter % Seq_chains[CHAIN_B].length];
		Song *s_a = NULL, *s_b = NULL;
		int retries = MAX_LOAD_RETRIES;
		while ((!s_a || !s_b) && retries-- > 0) {
			s_a = song_provider_cb(song_id_a, Seq_chains[CHAIN_A].project_index);
			s_b = song_provider_cb(song_id_b, Seq_chains[CHAIN_B].project_index);
			if (!s_a || !s_b) sleep_ms(WAIT_POLL_MS);
		}
		if (!s_a || !s_b || !play_songs(s_a, s_b, current_chain_track_selector)) break;
		chain_step_counter++;
	}
	is_playing = false;
	stop_seq_request = false;
}

void sequencer_stop(void) { stop_seq_request = true; }
void sequencer_set_tempo(uint8_t tempo) { current_tempo = tempo; }
uint8_t sequencer_get_tempo(void) { return current_tempo; }
void sequencer_set_track_selector(uint64_t selector) { current_chain_track_selector = selector; }
Chain* sequencer_get_chain(uint8_t chain_id) { return (chain_id < NUM_OF_CHAINS) ? &Seq_chains[chain_id] : NULL; }
bool sequencer_is_playing(void) { return is_playing; }
