#ifndef STORAGE_H
#define STORAGE_H

#include "../midi_types.h"
#include "../project_types.h"

#define MAX_SONGS_IN_PROJECT 256
#define MAX_FOLDERS_IN_DIR 256
#define DEFAULT_TEMPO_BPM 120
#define MAX_FILE_NAME_LEN 64
#define MAX_PATH_LEN 128
#define MAX_ACTIVE_PROJECTS 2

bool storage_init(void);
int storage_scan_folders(const char* path, char out_folder_list[][MAX_FILE_NAME_LEN], int max_folders);
bool storage_load_song(const char* project_path, uint8_t song_id, Song* song);
bool storage_save_song(const char* project_path, uint8_t song_id, Song* song);
void storage_free_song(Song* song);

bool event_storage_load(uint8_t song_id, bool previous_project);
void storage_set_project(const char* project_name);

Song* storage_get_loaded_song(uint8_t song_id, uint8_t project_index);
void storage_save_all(void);

#endif
