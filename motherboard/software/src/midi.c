#include "midi.h"
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

MidiPort midi_out_1;
MidiPort midi_out_2;

static PIO midi_pio = pio0;
static int pio_sm_1;
static int pio_sm_2;
static int dma_chan_1;
static int dma_chan_2;

static void dma_handler() {
	if (dma_hw->ints0 & DMA_INT_MASK(dma_chan_1)) {
		dma_hw->ints0 = DMA_INT_MASK(dma_chan_1);
		midi_out_1.is_busy = false;
	}
	if (dma_hw->ints0 & DMA_INT_MASK(dma_chan_2)) {
		dma_hw->ints0 = DMA_INT_MASK(dma_chan_2);
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
	
	dma_channel_set_irq0_enabled(dma_chan, true);
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
	
	irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
	irq_set_enabled(DMA_IRQ_0, true);
	
	midi_out_1.fill_buffer_A_next = true;
	midi_out_1.is_busy = false;
	midi_out_2.fill_buffer_A_next = true;
	midi_out_2.is_busy = false;
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
