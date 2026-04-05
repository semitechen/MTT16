#ifndef PROJECT_TYPES_H
#define PROJECT_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_SONGS_IN_CHAIN 16
#define CHAIN_A 0
#define CHAIN_B 1
#define NUM_OF_CHAINS 2

#define CURRENT_PROJECT_IDX 0
#define PREV_PROJECT_IDX 1
#define EMPTY_SLOT_ID 0xFF

typedef struct {
	uint8_t songs[MAX_SONGS_IN_CHAIN];
	uint8_t length;
	uint8_t project_index;
} Chain;

#endif
