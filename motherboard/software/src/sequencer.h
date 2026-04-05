#ifndef SEQUENCER_H
#define SEQUENCER_H

#include <stdint.h>
#include <stdbool.h>
#include "shared.h"

typedef Song* (*SongProvider)(uint8_t song_id, uint8_t project_index);

void sequencer_init(SongProvider provider);
void midi_out_init(void);
bool event_start_seq(void);
void sequencer_stop(void);

void sequencer_set_tempo(uint8_t tempo);
uint8_t sequencer_get_tempo(void);

void sequencer_set_track_selector(uint64_t selector);
Chain* sequencer_get_chain(uint8_t chain_id);

bool sequencer_is_playing(void);

#endif
