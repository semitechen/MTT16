#include "midi_driver.h"
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

typedef struct {
	volatile bool is_busy;
} MidiPortInternal;

static MidiPortInternal midi_ports[2];
static PIO midi_pio = pio1;
static int pio_sm[2];
static int dma_chan[2];

static void dma_handler() {
	if (dma_hw->ints1 & DMA_INT_MASK(dma_chan[0])) {
		dma_hw->ints1 = DMA_INT_MASK(dma_chan[0]);
		midi_ports[0].is_busy = false;
	}
	if (dma_hw->ints1 & DMA_INT_MASK(dma_chan[1])) {
		dma_hw->ints1 = DMA_INT_MASK(dma_chan[1]);
		midi_ports[1].is_busy = false;
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

static void setup_dma(int chan, int sm) {
	dma_channel_config c = dma_channel_get_default_config(chan);
	channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
	channel_config_set_read_increment(&c, true);
	channel_config_set_write_increment(&c, false);
	channel_config_set_dreq(&c, pio_get_dreq(midi_pio, sm, true));
	
	dma_channel_configure(
		chan,
		&c,
		&midi_pio->txf[sm],
		NULL,
		DMA_RING_NONE,
		false
	);
	dma_channel_set_irq1_enabled(chan, true);
}

void midi_driver_init(void) {
	uint offset = pio_add_program(midi_pio, &midi_tx_program);
	
	pio_sm[0] = pio_claim_unused_sm(midi_pio, true);
	pio_sm[1] = pio_claim_unused_sm(midi_pio, true);
	dma_chan[0] = dma_claim_unused_channel(true);
	dma_chan[1] = dma_claim_unused_channel(true);
	
	setup_pio_sm(pio_sm[0], MIDI_OUT1_PIN, offset);
	setup_pio_sm(pio_sm[1], MIDI_OUT2_PIN, offset);
	
	setup_dma(dma_chan[0], pio_sm[0]);
	setup_dma(dma_chan[1], pio_sm[1]);
	
	irq_set_exclusive_handler(DMA_IRQ_1, dma_handler);
	irq_set_enabled(DMA_IRQ_1, true);
	
	midi_ports[0].is_busy = false;
	midi_ports[1].is_busy = false;
}

void midi_driver_send(uint8_t port_idx, const uint8_t* data, uint16_t length) {
	if (port_idx >= 2 || length == 0) return;
	
	while (midi_ports[port_idx].is_busy) {
		tight_loop_contents();
	}
	
	midi_ports[port_idx].is_busy = true;
	dma_channel_set_read_addr(dma_chan[port_idx], data, false);
	dma_channel_set_trans_count(dma_chan[port_idx], length, true);
}

bool midi_driver_is_busy(uint8_t port_idx) {
	if (port_idx >= 2) return false;
	return midi_ports[port_idx].is_busy;
}
