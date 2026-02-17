#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#define INTERNAL_PPQN 96
#define TICKS_PER_STEP (INTERNAL_PPQN / 4)

#define MAX_MUSIC_TRACKS 16
#define MAX_TRACKS 17
#define CONFIG_TRACK_INDEX 16

#define MAX_SONGS_IN_PROJECT 256
#define MAX_FOLDERS_IN_DIR 256

#define DEFAULT_TEMPO_BPM 120

#define MAX_FILE_NAME_LEN 64
#define MAX_PATH_LEN 128

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
    Track tracks[MAX_TRACKS];
} Song;

bool storage_init(void);
int storage_scan_folders(const char* path, char out_folder_list[][MAX_FILE_NAME_LEN], int max_folders);
bool storage_load_song(const char* song_path, Song* song);
bool storage_save_song(const char* song_path, Song* song);
void storage_free_song(Song* song);

#endif
