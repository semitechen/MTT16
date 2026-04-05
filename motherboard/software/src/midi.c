#include "midi.h"
#include "shared.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "midi_tx.pio.h"

#define MIDI_OUT1_PIN 28
#define MIDI_OUT2_PIN 27
#define MIDI_BAUD_RATE 31250
#define PIO_OVERSAMPLE 8
#define PIO_SHIFT_SIZE 32
#define PIO_PIN_COUNT 1
#define DMA_RING_NONE 0
#define DMA_INT_MASK(chan) (1u << (chan))

#define USEC_PER_MINUTE 60000000
#define STEPS_PER_BEAT 4
#define MICRO_DELAY_MAX 256
#define MAX_EVENTS_PER_STEP 256
#define WAIT_POLL_MS 10
#define MIDI_REALTIME_MASK 0xF8
#define MIDI_TYPE_MASK 0xF0
#define MIDI_PROG_CHANGE 0xC0
#define MIDI_CHAN_PRESSURE 0xD0

MidiPort midi_out_1;
MidiPort midi_out_2;

static PIO midi_pio = pio1;
static int pio_sm_1;
static int pio_sm_2;
static int dma_chan_1;
static int dma_chan_2;

static void dma_handler() {
	if (dma_hw->ints1 & DMA_INT_MASK(dma_chan_1)) {
		dma_hw->ints1 = DMA_INT_MASK(dma_chan_1);
		midi_out_1.is_busy = false;
	}
	if (dma_hw->ints1 & DMA_INT_MASK(dma_chan_2)) {
		dma_hw->ints1 = DMA_INT_MASK(dma_chan_2);
		midi_out_2.is_busy = false;
	}
}

static void setup_pio_sm(int sm, uint pin, uint offset) {
	pio_sm_config c = midi_tx_program_get_default_config(offset);
	sm_config_set_out_shift(&c, true, false, PIO_SHIFT_SIZE);
	sm_config_set_out_pins(&c, pin, PIO_PIN_COUNT);
	sm_config_set_sideset_pins(&c, pin);
	sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
	
	float div = (float)clock_get_hz(clk_sys) / (PIO_OVERSAMPLE * MIDI_BAUD_RATE);
	sm_config_set_clkdiv(&c, div);
	
	pio_gpio_init(midi_pio, pin);
	pio_sm_set_consecutive_pindirs(midi_pio, sm, pin, PIO_PIN_COUNT, true);
	pio_sm_init(midi_pio, sm, offset, &c);
	pio_sm_set_enabled(midi_pio, sm, true);
}

static void setup_dma(int dma_chan, int pio_sm) {
	dma_channel_config c = dma_channel_get_default_config(dma_chan);
	
	channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
	channel_config_set_read_increment(&c, true);
	channel_config_set_write_increment(&c, false);
	channel_config_set_dreq(&c, pio_get_dreq(midi_pio, pio_sm, true));
	
	dma_channel_configure(
		dma_chan,
		&c,
		&midi_pio->txf[pio_sm],
		NULL,
		DMA_RING_NONE,
		false
	);
	
	dma_channel_set_irq1_enabled(dma_chan, true);
}

void midi_out_init(void) {
	uint offset = pio_add_program(midi_pio, &midi_tx_program);
	
	pio_sm_1 = pio_claim_unused_sm(midi_pio, true);
	pio_sm_2 = pio_claim_unused_sm(midi_pio, true);
	dma_chan_1 = dma_claim_unused_channel(true);
	dma_chan_2 = dma_claim_unused_channel(true);
	
	setup_pio_sm(pio_sm_1, MIDI_OUT1_PIN, offset);
	setup_pio_sm(pio_sm_2, MIDI_OUT2_PIN, offset);
	
	setup_dma(dma_chan_1, pio_sm_1);
	setup_dma(dma_chan_2, pio_sm_2);
	
	irq_set_exclusive_handler(DMA_IRQ_1, dma_handler);
	irq_set_enabled(DMA_IRQ_1, true);
	
	midi_out_1.fill_buffer_A_next = true;
	midi_out_1.is_busy = false;
	midi_out_1.length_A = 0;
	midi_out_1.length_B = 0;
	
	midi_out_2.fill_buffer_A_next = true;
	midi_out_2.is_busy = false;
	midi_out_2.length_A = 0;
	midi_out_2.length_B = 0;
}

void midi_send(MidiPort *port) {
	if (port->is_busy) return;
	
	port->is_busy = true;
	int target_dma = (port == &midi_out_1) ? dma_chan_1 : dma_chan_2;
	
	if (port->fill_buffer_A_next) {
		dma_channel_set_read_addr(target_dma, port->buffer_B, false);
		dma_channel_set_trans_count(target_dma, port->length_B, true);
	} else {
		dma_channel_set_read_addr(target_dma, port->buffer_A, false);
		dma_channel_set_trans_count(target_dma, port->length_A, true);
	}
}

static uint8_t get_midi_msg_len(uint8_t status) {
	if (status >= MIDI_REALTIME_MASK) return 1;
	uint8_t type = status & MIDI_TYPE_MASK;
	if (type == MIDI_PROG_CHANGE || type == MIDI_CHAN_PRESSURE) return 2;
	return 3;
}

static void push_to_port(MidiPort* port, MidiEvent* ev) {
	uint8_t len = get_midi_msg_len(ev->status);
	uint16_t* current_len = port->fill_buffer_A_next ? &port->length_A : &port->length_B;
	uint8_t* buf = port->fill_buffer_A_next ? port->buffer_A : port->buffer_B;

	if (*current_len + len > MIDI_BUFFER_SIZE) {
		while (port->is_busy) {
			tight_loop_contents();
		}
		port->fill_buffer_A_next = !port->fill_buffer_A_next;
		midi_send(port);
		current_len = port->fill_buffer_A_next ? &port->length_A : &port->length_B;
		buf = port->fill_buffer_A_next ? port->buffer_A : port->buffer_B;
		*current_len = 0;
	}

	buf[*current_len + 0] = ev->status;
	if (len > 1) buf[*current_len + 1] = ev->data1;
	if (len > 2) buf[*current_len + 2] = ev->data2;
	*current_len += len;
}

static void flush_port(MidiPort* port) {
	uint16_t* current_len = port->fill_buffer_A_next ? &port->length_A : &port->length_B;
	if (*current_len > 0) {
		while (port->is_busy) {
			tight_loop_contents();
		}
		port->fill_buffer_A_next = !port->fill_buffer_A_next;
		midi_send(port);
		current_len = port->fill_buffer_A_next ? &port->length_A : &port->length_B;
		*current_len = 0;
	}
}

void play_song(uint8_t song_id, uint8_t project_index) {
	Song* song = NULL;
	
	while (!song) {
		for (int i = 0; i < MAX_LOADED_SONGS; i++) {
			if (ring_song_ids[i] == song_id && loaded_songs[i].project_index == project_index && loaded_songs[i].tempo > 0) {
				song = &loaded_songs[i];
				break;
			}
		}
		if (!song) {
			sleep_ms(WAIT_POLL_MS);
		}
	}

	uint32_t step_us = USEC_PER_MINUTE / (song->tempo * STEPS_PER_BEAT);
	uint32_t track_indices[MAX_MUSIC_TRACKS] = {0};
	uint32_t current_step = 0;

	midi_out_1.length_A = 0;
	midi_out_1.length_B = 0;
	midi_out_2.length_A = 0;
	midi_out_2.length_B = 0;

	absolute_time_t next_step_time = get_absolute_time();

	while (true) {
		MidiEvent* step_events[MAX_EVENTS_PER_STEP];
		int step_event_count = 0;
		bool any_track_active = false;

		for (int t = 0; t < MAX_MUSIC_TRACKS; t++) {
			Track* trk = &song->tracks[t];
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

		if (!any_track_active && step_event_count == 0) {
			break;
		}

		for (int i = 1; i < step_event_count; i++) {
			MidiEvent* key = step_events[i];
			int j = i - 1;
			while (j >= 0 && step_events[j]->micro_delay > key->micro_delay) {
				step_events[j + 1] = step_events[j];
				j--;
			}
			step_events[j + 1] = key;
		}

		int i = 0;
		while (i < step_event_count) {
			uint8_t current_delay = step_events[i]->micro_delay;
			uint32_t delay_us = (step_us * current_delay) / MICRO_DELAY_MAX;
			absolute_time_t ev_time = delayed_by_us(next_step_time, delay_us);

			sleep_until(ev_time);

			while (i < step_event_count && step_events[i]->micro_delay == current_delay) {
				push_to_port(&midi_out_1, step_events[i]);
				push_to_port(&midi_out_2, step_events[i]);
				i++;
			}
			
			flush_port(&midi_out_1);
			flush_port(&midi_out_2);
		}

		next_step_time = delayed_by_us(next_step_time, step_us);
		sleep_until(next_step_time);
		current_step++;
	}
}
