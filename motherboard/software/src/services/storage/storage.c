#include "storage.h"
#include "../../drivers/sd/sd_driver.h"
#include "ff.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define CHUNK_ID_MTHD "MThd"
#define CHUNK_ID_MTRK "MTrk"
#define CHUNK_ID_LEN 4
#define MTHD_DATA_LEN 6
#define MTHD_FORMAT_0 0
#define MTHD_ONE_TRACK 1
#define MIDI_STATUS_VOICE_MIN 0x80
#define MIDI_STATUS_VOICE_MAX 0xE0
#define MIDI_STATUS_PROG_CHG 0xC0
#define MIDI_STATUS_CH_AT 0xD0
#define MIDI_STATUS_SYSEX 0xF0
#define MIDI_STATUS_SYSEX_END 0xF7
#define MIDI_STATUS_META 0xFF
#define META_TYPE_TEMPO 0x51
#define META_TYPE_EOT 0x2F
#define META_LEN_TEMPO 3
#define META_LEN_EOT 0
#define MICROSECONDS_PER_MINUTE 60000000
#define MAX_MICRO_DELAY 255
#define MSB_MASK 0x80
#define DATA_MASK 0x7F
#define HEX_PREFIX_LEN 2
#define HEX_SHIFT_BITS 4
#define HEX_ALPHA_OFFSET 10
#define INVALID_HEX_ID -1
#define PATH_FMT_SCAN "%s/%s"
#define PATH_FMT_NEW "%s/%02X"
#define STR_CONFIG_FILE "config.mid"
#define STR_CONFIG_FILE_UPPER "CONFIG.MID"
#define FILE_EXT_LOWER ".mid"
#define FILE_EXT_UPPER ".MID"
#define MIN_MUSIC_TRACK_NUM 1
#define MAX_MUSIC_TRACK_NUM 16

#define INTERNAL_PPQN 96
#define TICKS_PER_STEP (INTERNAL_PPQN / 4)

static Song loaded_songs[MAX_LOADED_SONGS];
static uint8_t ring_song_ids[MAX_LOADED_SONGS];
static MidiEvent event_pool[MAX_LOADED_SONGS][MAX_EVENTS_PER_SONG];
static uint32_t event_pool_used[MAX_LOADED_SONGS];
static char active_projects[MAX_ACTIVE_PROJECTS][MAX_PATH_LEN];
static uint8_t ring_idx = 0;

static uint16_t read_be16(FIL *fil) {
	uint8_t bytes[2]; UINT br;
	f_read(fil, bytes, 2, &br);
	return (br < 2) ? 0 : (bytes[0] << 8) | bytes[1];
}
static uint32_t read_be32(FIL *fil) {
	uint8_t bytes[4]; UINT br;
	f_read(fil, bytes, 4, &br);
	return (br < 4) ? 0 : (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
}
static void write_be16(FIL *fil, uint16_t val) {
	uint8_t bytes[2] = {(val >> 8) & 0xFF, val & 0xFF};
	UINT bw; f_write(fil, bytes, 2, &bw);
}
static void write_be32(FIL *fil, uint32_t val) {
	uint8_t bytes[4] = {(val >> 24) & 0xFF, (val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF};
	UINT bw; f_write(fil, bytes, 4, &bw);
}
static uint32_t read_vlq(FIL *fil, uint32_t *bytes_read_out) {
	uint32_t value = 0, cnt = 0; uint8_t byte; UINT br;
	do { f_read(fil, &byte, 1, &br); if (br == 0) break; value = (value << 7) | (byte & DATA_MASK); cnt++; } while (byte & MSB_MASK);
	if (bytes_read_out) *bytes_read_out += cnt;
	return value;
}
static void write_vlq(FIL *fil, uint32_t value) {
	uint8_t temp[5]; int i = 0; temp[i++] = value & DATA_MASK;
	while ((value >>= 7)) temp[i++] = (value & DATA_MASK) | MSB_MASK;
	while (i > 0) { UINT bw; f_write(fil, &temp[--i], 1, &bw); }
}
static void skip_bytes(FIL *fil, uint32_t len) { f_lseek(fil, f_tell(fil) + len); }
static int compare_events(const void* a, const void* b) {
	MidiEvent *e1 = (MidiEvent*)a, *e2 = (MidiEvent*)b;
	if (e1->step != e2->step) return (e1->step < e2->step) ? -1 : 1;
	return (e1->micro_delay < e2->micro_delay) ? -1 : (e1->micro_delay > e2->micro_delay);
}

static void track_alloc_ram(FIL *fil, uint32_t chunk_len, uint32_t *event_count, uint16_t *tempo_out) {
	uint32_t bytes_processed = 0; uint8_t last_status = 0; *event_count = 0;
	while (bytes_processed < chunk_len) {
		uint32_t start_pos = f_tell(fil); read_vlq(fil, NULL); bytes_processed += (f_tell(fil) - start_pos);
		uint8_t status; UINT br; f_read(fil, &status, 1, &br); if (br == 0) break; bytes_processed++;
		if (status < MIDI_STATUS_VOICE_MIN) { status = last_status; f_lseek(fil, f_tell(fil) - 1); bytes_processed--; } else last_status = status;
		if (status == MIDI_STATUS_META) {
			uint8_t type; f_read(fil, &type, 1, &br); bytes_processed++;
			start_pos = f_tell(fil); uint32_t len = read_vlq(fil, NULL); bytes_processed += (f_tell(fil) - start_pos);
			if (type == META_TYPE_TEMPO && len == META_LEN_TEMPO && tempo_out) {
				uint8_t t[3]; f_read(fil, t, 3, &br);
				uint32_t us_per_qn = (t[0] << 16) | (t[1] << 8) | t[2];
				if (us_per_qn > 0) *tempo_out = MICROSECONDS_PER_MINUTE / us_per_qn;
			} else skip_bytes(fil, len);
			bytes_processed += len;
		} else if (status == MIDI_STATUS_SYSEX || status == MIDI_STATUS_SYSEX_END) {
			start_pos = f_tell(fil); uint32_t len = read_vlq(fil, NULL); bytes_processed += (f_tell(fil) - start_pos);
			skip_bytes(fil, len); bytes_processed += len;
		} else if ((status & MIDI_STATUS_SYSEX) >= MIDI_STATUS_VOICE_MIN && (status & MIDI_STATUS_SYSEX) <= MIDI_STATUS_VOICE_MAX) {
			uint8_t data_bytes = ((status & MIDI_STATUS_VOICE_MAX) == MIDI_STATUS_PROG_CHG || (status & MIDI_STATUS_SYSEX) == MIDI_STATUS_CH_AT) ? 1 : 2;
			skip_bytes(fil, data_bytes); bytes_processed += data_bytes; (*event_count)++;
		} else break;
	}
}

static void track_load_data(FIL *fil, uint32_t chunk_len, Track *track, uint16_t file_ppqn) {
	uint32_t bytes_processed = 0, abs_file_ticks = 0, ev_idx = 0; uint8_t last_status = 0;
	while (bytes_processed < chunk_len && ev_idx < track->capacity) {
		uint32_t start_pos = f_tell(fil); uint32_t delta = read_vlq(fil, NULL); bytes_processed += (f_tell(fil) - start_pos);
		abs_file_ticks += delta;
		uint8_t status; UINT br; f_read(fil, &status, 1, &br); if (br == 0) break; bytes_processed++;
		if (status < MIDI_STATUS_VOICE_MIN) { status = last_status; f_lseek(fil, f_tell(fil) - 1); bytes_processed--; } else last_status = status;
		if (status == MIDI_STATUS_META) {
			f_read(fil, &status, 1, &br); bytes_processed++;
			start_pos = f_tell(fil); uint32_t len = read_vlq(fil, NULL); bytes_processed += (f_tell(fil) - start_pos);
			skip_bytes(fil, len); bytes_processed += len;
		} else if (status == MIDI_STATUS_SYSEX || status == MIDI_STATUS_SYSEX_END) {
			start_pos = f_tell(fil); uint32_t len = read_vlq(fil, NULL); bytes_processed += (f_tell(fil) - start_pos);
			skip_bytes(fil, len); bytes_processed += len;
		} else if ((status & MIDI_STATUS_SYSEX) >= MIDI_STATUS_VOICE_MIN && (status & MIDI_STATUS_SYSEX) <= MIDI_STATUS_VOICE_MAX) {
			MidiEvent *ev = &track->events[ev_idx];
			uint32_t abs_internal = (uint32_t)(((uint64_t)abs_file_ticks * INTERNAL_PPQN) / file_ppqn);
			ev->step = abs_internal / TICKS_PER_STEP;
			ev->micro_delay = ((abs_internal % TICKS_PER_STEP) * MAX_MICRO_DELAY) / TICKS_PER_STEP;
			ev->status = status; f_read(fil, &ev->data1, 1, &br); bytes_processed++;
			if ((status & MIDI_STATUS_VOICE_MAX) == MIDI_STATUS_PROG_CHG || (status & MIDI_STATUS_SYSEX) == MIDI_STATUS_CH_AT) ev->data2 = 0;
			else { f_read(fil, &ev->data2, 1, &br); bytes_processed++; }
			ev_idx++;
		}
	}
	track->event_count = ev_idx;
	if (track->event_count > 0) qsort(track->events, track->event_count, sizeof(MidiEvent), compare_events);
}

bool storage_init(void) {
	if (!sd_driver_init()) return false;
	for (int i = 0; i < MAX_LOADED_SONGS; i++) { event_pool_used[i] = 0; ring_song_ids[i] = EMPTY_SLOT_ID; }
	return true;
}

int storage_scan_folders(const char* path, char out_folder_list[][MAX_FILE_NAME_LEN], int max_folders) {
	DIR dir; FILINFO fno; int folder_count = 0;
	if (f_opendir(&dir, path) != FR_OK) return 0;
	while (folder_count < max_folders) {
		if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
		if (fno.fname[0] == '.' || !(fno.fattrib & AM_DIR)) continue;
		strncpy(out_folder_list[folder_count], fno.fname, MAX_FILE_NAME_LEN - 1);
		out_folder_list[folder_count][MAX_FILE_NAME_LEN - 1] = '\0';
		folder_count++;
	}
	f_closedir(&dir);
	return folder_count;
}

void storage_free_song(Song* song) {
	if (!song) return;
	int slot = song - loaded_songs;
	if (slot >= 0 && slot < MAX_LOADED_SONGS) event_pool_used[slot] = 0;
	for (int i = 0; i < MAX_TRACKS; i++) { song->tracks[i].events = NULL; song->tracks[i].event_count = 0; song->tracks[i].capacity = 0; song->tracks[i].is_modified = false; }
}

static bool get_song_path(const char* project_path, uint8_t song_id, char* out_path) {
	DIR dir; FILINFO fno; bool found = false;
	if (f_opendir(&dir, project_path) == FR_OK) {
		while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
			if (!(fno.fattrib & AM_DIR) || fno.fname[0] == '.') continue;
			int val = 0;
			for (int i = 0; i < 2; i++) {
				char c = fno.fname[i]; val <<= 4;
				if (c >= '0' && c <= '9') val |= (c - '0');
				else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
				else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
				else { val = -1; break; }
			}
			if (val == song_id) { snprintf(out_path, MAX_PATH_LEN, "%s/%s", project_path, fno.fname); found = true; break; }
		}
		f_closedir(&dir);
	}
	if (!found) snprintf(out_path, MAX_PATH_LEN, "%s/%02X", project_path, song_id);
	return found;
}

bool storage_load_song(const char* project_path, uint8_t song_id, Song* song) {
	storage_free_song(song); memset(song, 0, sizeof(Song)); song->tempo = DEFAULT_TEMPO_BPM;
	char song_path[MAX_PATH_LEN];
	if (!get_song_path(project_path, song_id, song_path)) { if (f_mkdir(song_path) == FR_OK) return true; return false; }
	DIR dir; FILINFO fno; if (f_opendir(&dir, song_path) != FR_OK) return false;
	while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
		if (fno.fattrib & AM_DIR || fno.fname[0] == '.') continue;
		char *ext = strrchr(fno.fname, '.'); if (!ext || (strcmp(ext, ".mid") != 0 && strcmp(ext, ".MID") != 0)) continue;
		int target_idx = -1; bool is_config = false;
		if (strcmp(fno.fname, "config.mid") == 0 || strcmp(fno.fname, "CONFIG.MID") == 0) { target_idx = CONFIG_TRACK_INDEX; is_config = true; }
		else { int num = atoi(fno.fname); if (num >= 1 && num <= 16) target_idx = num - 1; }
		if (target_idx >= 0) {
			char filepath[MAX_PATH_LEN]; snprintf(filepath, MAX_PATH_LEN, "%s/%s", song_path, fno.fname);
			FIL fil; if (f_open(&fil, filepath, FA_READ) == FR_OK) {
				char chunk_id[5] = {0}; UINT br; f_read(&fil, chunk_id, 4, &br);
				if (strncmp(chunk_id, "MThd", 4) == 0) {
					uint32_t header_len = read_be32(&fil); read_be16(&fil); uint16_t num_tracks = read_be16(&fil); uint16_t ppqn = read_be16(&fil);
					if (num_tracks > 0) {
						if (header_len > 6) skip_bytes(&fil, header_len - 6);
						while (f_read(&fil, chunk_id, 4, &br) == FR_OK && br == 4) {
							uint32_t chunk_len = read_be32(&fil);
							if (strncmp(chunk_id, "MTrk", 4) == 0) {
								DWORD chunk_start = f_tell(&fil); uint32_t ev_count = 0;
								track_alloc_ram(&fil, chunk_len, &ev_count, is_config ? &song->tempo : NULL);
								Track *t = &song->tracks[target_idx];
								int slot = song - loaded_songs;
								if (ev_count > 0 && slot >= 0 && slot < MAX_LOADED_SONGS && (event_pool_used[slot] + ev_count) <= MAX_EVENTS_PER_SONG) {
									t->events = &event_pool[slot][event_pool_used[slot]];
									event_pool_used[slot] += ev_count; t->capacity = ev_count;
									f_lseek(&fil, chunk_start); track_load_data(&fil, chunk_len, t, ppqn);
								}
								t->is_modified = false; break;
							} else skip_bytes(&fil, chunk_len);
						}
					}
				}
				f_close(&fil);
			}
		}
	}
	f_closedir(&dir); return true;
}

bool storage_save_song(const char* project_path, uint8_t song_id, Song* song) {
	char song_path[MAX_PATH_LEN]; get_song_path(project_path, song_id, song_path); f_mkdir(song_path);
	for (int i = 0; i < MAX_TRACKS; i++) {
		Track *t = &song->tracks[i]; if (!t->is_modified) continue;
		bool is_config = (i == CONFIG_TRACK_INDEX); char filepath[MAX_PATH_LEN];
		if (is_config) snprintf(filepath, MAX_PATH_LEN, "%s/config.mid", song_path);
		else snprintf(filepath, MAX_PATH_LEN, "%s/%d.mid", song_path, i + 1);
		FIL fil; if (f_open(&fil, filepath, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) continue;
		UINT bw; f_write(&fil, "MThd", 4, &bw); write_be32(&fil, 6); write_be16(&fil, 0); write_be16(&fil, 1); write_be16(&fil, INTERNAL_PPQN);
		if (t->event_count > 0) qsort(t->events, t->event_count, sizeof(MidiEvent), compare_events);
		f_write(&fil, "MTrk", 4, &bw); DWORD len_pos = f_tell(&fil); write_be32(&fil, 0); DWORD chunk_start = f_tell(&fil);
		if (is_config && song->tempo > 0) {
			write_vlq(&fil, 0); uint8_t tempo_header[2] = {MIDI_STATUS_META, META_TYPE_TEMPO}; f_write(&fil, tempo_header, 2, &bw);
			write_vlq(&fil, 3); uint32_t us_per_qn = MICROSECONDS_PER_MINUTE / song->tempo;
			uint8_t temp[3] = {(us_per_qn >> 16) & 0xFF, (us_per_qn >> 8) & 0xFF, us_per_qn & 0xFF}; f_write(&fil, temp, 3, &bw);
		}
		uint32_t last_tick = 0;
		for (uint32_t j = 0; j < t->event_count; j++) {
			MidiEvent *ev = &t->events[j];
			uint32_t current_tick = (ev->step * TICKS_PER_STEP) + ((ev->micro_delay * TICKS_PER_STEP) / MAX_MICRO_DELAY);
			if (current_tick < last_tick) current_tick = last_tick;
			write_vlq(&fil, current_tick - last_tick); last_tick = current_tick;
			f_write(&fil, &ev->status, 1, &bw); f_write(&fil, &ev->data1, 1, &bw);
			if ((ev->status & MIDI_STATUS_VOICE_MAX) != MIDI_STATUS_PROG_CHG && (ev->status & MIDI_STATUS_SYSEX) != MIDI_STATUS_CH_AT) f_write(&fil, &ev->data2, 1, &bw);
		}
		write_vlq(&fil, 0); uint8_t eot[3] = {MIDI_STATUS_META, META_TYPE_EOT, 0}; f_write(&fil, eot, 3, &bw);
		DWORD chunk_end = f_tell(&fil); f_lseek(&fil, len_pos); write_be32(&fil, chunk_end - chunk_start); f_lseek(&fil, chunk_end);
		f_close(&fil); t->is_modified = false;
	}
	return true;
}

void storage_set_project(const char* project_name) {
	strncpy(active_projects[PREV_PROJECT_IDX], active_projects[CURRENT_PROJECT_IDX], MAX_PATH_LEN - 1);
	strncpy(active_projects[CURRENT_PROJECT_IDX], project_name, MAX_PATH_LEN - 1);
}

bool event_storage_load(uint8_t song_id, bool previous_project) {
	uint8_t target_proj = previous_project ? PREV_PROJECT_IDX : CURRENT_PROJECT_IDX;
	for (int i = 0; i < MAX_LOADED_SONGS; i++) {
		if (ring_song_ids[i] == song_id && loaded_songs[i].project_index == target_proj) {
			for (int j = 0; j < MAX_LOADED_SONGS; j++) if (ring_song_ids[j] != EMPTY_SLOT_ID) storage_save_song(active_projects[loaded_songs[j].project_index], ring_song_ids[j], &loaded_songs[j]);
			return true;
		}
	}
	if (ring_song_ids[ring_idx] != EMPTY_SLOT_ID) {
		storage_save_song(active_projects[loaded_songs[ring_idx].project_index], ring_song_ids[ring_idx], &loaded_songs[ring_idx]);
		storage_free_song(&loaded_songs[ring_idx]);
	}
	if (storage_load_song(active_projects[target_proj], song_id, &loaded_songs[ring_idx])) {
		loaded_songs[ring_idx].project_index = target_proj; ring_song_ids[ring_idx] = song_id;
		ring_idx = (ring_idx + 1) % MAX_LOADED_SONGS; return true;
	}
	ring_song_ids[ring_idx] = EMPTY_SLOT_ID; return false;
}

Song* storage_get_loaded_song(uint8_t song_id, uint8_t project_index) {
	for (int i = 0; i < MAX_LOADED_SONGS; i++) if (ring_song_ids[i] == song_id && loaded_songs[i].project_index == project_index) return &loaded_songs[i];
	return NULL;
}

void storage_save_all(void) {
	for (int i = 0; i < MAX_LOADED_SONGS; i++) if (ring_song_ids[i] != EMPTY_SLOT_ID) storage_save_song(active_projects[loaded_songs[i].project_index], ring_song_ids[i], &loaded_songs[i]);
}
