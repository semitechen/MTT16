#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_MUSIC_TRACKS 16
#define MAX_TRACKS 17
#define CONFIG_TRACK_INDEX 16

#define MAX_LOADED_SONGS 4
#define MAX_EVENTS_PER_SONG 4096

#define CURRENT_PROJECT_IDX 0
#define PREV_PROJECT_IDX 1
#define EMPTY_SLOT_ID 0xFF

typedef struct {
	uint32_t step;
	uint8_t micro_delay;
	uint8_t status;
	uint8_t data1;
	uint8_t data2;
} MidiEvent;

typedef struct {
	MidiEvent* events;
	uint32_t event_count;
	uint32_t capacity;
	bool is_modified;
} Track;

typedef struct {
	uint16_t tempo;
	uint8_t project_index;
	Track tracks[MAX_TRACKS];
} Song;

extern Song loaded_songs[MAX_LOADED_SONGS];
extern uint8_t ring_song_ids[MAX_LOADED_SONGS];

#endif
