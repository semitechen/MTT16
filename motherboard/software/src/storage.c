#include "storage.h"
#include "sd_config/hw_config.h"
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

Song loaded_songs[MAX_LOADED_SONGS];

static MidiEvent event_pool[MAX_LOADED_SONGS][MAX_EVENTS_PER_SONG];
static uint32_t event_pool_used[MAX_LOADED_SONGS];

static FATFS fs;
static bool fs_mounted = false;

char active_projects[MAX_ACTIVE_PROJECTS][MAX_PATH_LEN];

static uint8_t ring_song_ids[MAX_LOADED_SONGS];
static uint8_t ring_idx = 0;

static uint16_t read_be16(FIL *fil) {
	uint8_t bytes[2];
	UINT br;
	f_read(fil, bytes, 2, &br);
	if (br < 2) return 0;
	return (bytes[0] << 8) | bytes[1];
}

static uint32_t read_be32(FIL *fil) {
	uint8_t bytes[4];
	UINT br;
	f_read(fil, bytes, 4, &br);
	if (br < 4) return 0;
	return (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
}

static void write_be16(FIL *fil, uint16_t val) {
	uint8_t bytes[2];
	bytes[0] = (val >> 8) & 0xFF;
	bytes[1] = val & 0xFF;
	UINT bw;
	f_write(fil, bytes, 2, &bw);
}

static void write_be32(FIL *fil, uint32_t val) {
	uint8_t bytes[4];
	bytes[0] = (val >> 24) & 0xFF;
	bytes[1] = (val >> 16) & 0xFF;
	bytes[2] = (val >> 8) & 0xFF;
	bytes[3] = val & 0xFF;
	UINT bw;
	f_write(fil, bytes, 4, &bw);
}

static uint32_t read_vlq(FIL *fil, uint32_t *bytes_read_out) {
	uint32_t value = 0;
	uint8_t byte;
	UINT br;
	uint32_t cnt = 0;

	do {
		f_read(fil, &byte, 1, &br);
		if (br == 0) break;
		value = (value << 7) | (byte & DATA_MASK);
		cnt++;
	} while (byte & MSB_MASK);

	if (bytes_read_out) *bytes_read_out += cnt;
	return value;
}

static void write_vlq(FIL *fil, uint32_t value) {
	uint8_t temp[5];
	int i = 0;
	temp[i++] = value & DATA_MASK;
	while ((value >>= 7)) {
		temp[i++] = (value & DATA_MASK) | MSB_MASK;
	}
	while (i > 0) {
		UINT bw;
		f_write(fil, &temp[--i], 1, &bw);
	}
}

static void skip_bytes(FIL *fil, uint32_t len) {
	f_lseek(fil, f_tell(fil) + len);
}

static int compare_events(const void* a, const void* b) {
	const MidiEvent* ev1 = (const MidiEvent*)a;
	const MidiEvent* ev2 = (const MidiEvent*)b;
	
	if (ev1->step != ev2->step) {
		return (ev1->step < ev2->step) ? -1 : 1;
	}
	if (ev1->micro_delay != ev2->micro_delay) {
		return (ev1->micro_delay < ev2->micro_delay) ? -1 : 1;
	}
	return 0;
}

static void track_alloc_ram(FIL *fil, uint32_t chunk_len, uint32_t *event_count, uint16_t *tempo_out) {
	uint32_t bytes_processed = 0;
	uint8_t last_status = 0;
	*event_count = 0;

	while (bytes_processed < chunk_len) {
		uint32_t vlq_len = 0;
		uint32_t start_pos = f_tell(fil);
		read_vlq(fil, &vlq_len);
		bytes_processed += (f_tell(fil) - start_pos);

		uint8_t status;
		UINT br;
		f_read(fil, &status, 1, &br);
		if (br == 0) break;
		bytes_processed++;

		if (status < MIDI_STATUS_VOICE_MIN) {
			status = last_status;
			f_lseek(fil, f_tell(fil) - 1); 
			bytes_processed--; 
		} else {
			last_status = status;
		}

		if (status == MIDI_STATUS_META) {
			uint8_t type;
			f_read(fil, &type, 1, &br);
			if (br == 0) break;
			bytes_processed++;
			
			uint32_t len_bytes = 0;
			start_pos = f_tell(fil);
			uint32_t len = read_vlq(fil, &len_bytes);
			bytes_processed += (f_tell(fil) - start_pos);

			if (type == META_TYPE_TEMPO && len == META_LEN_TEMPO && tempo_out) {
				uint8_t t[META_LEN_TEMPO];
				f_read(fil, t, META_LEN_TEMPO, &br);
				if (br < META_LEN_TEMPO) break;
				uint32_t us_per_qn = (t[0] << 16) | (t[1] << 8) | t[2];
				if (us_per_qn > 0) *tempo_out = MICROSECONDS_PER_MINUTE / us_per_qn;
			} else {
				skip_bytes(fil, len);
			}
			bytes_processed += len;
		} 
		else if (status == MIDI_STATUS_SYSEX || status == MIDI_STATUS_SYSEX_END) {
			uint32_t len_bytes = 0;
			start_pos = f_tell(fil);
			uint32_t len = read_vlq(fil, &len_bytes);
			bytes_processed += (f_tell(fil) - start_pos);
			skip_bytes(fil, len);
			bytes_processed += len;
		} 
		else if ((status & MIDI_STATUS_SYSEX) >= MIDI_STATUS_VOICE_MIN && (status & MIDI_STATUS_SYSEX) <= MIDI_STATUS_VOICE_MAX) {
			uint8_t data_bytes = ((status & MIDI_STATUS_VOICE_MAX) == MIDI_STATUS_PROG_CHG || (status & MIDI_STATUS_SYSEX) == MIDI_STATUS_CH_AT) ? 1 : 2;
			skip_bytes(fil, data_bytes);
			bytes_processed += data_bytes;
			(*event_count)++;
		} 
		else {
			 break;
		}
	}
}

static void track_load_data(FIL *fil, uint32_t chunk_len, Track *track, uint16_t file_ppqn) {
	uint32_t bytes_processed = 0;
	uint8_t last_status = 0;
	uint32_t abs_file_ticks = 0;
	uint32_t ev_idx = 0;

	while (bytes_processed < chunk_len && ev_idx < track->capacity) {
		uint32_t vlq_len = 0;
		uint32_t start_pos = f_tell(fil);
		uint32_t delta = read_vlq(fil, &vlq_len);
		bytes_processed += (f_tell(fil) - start_pos);
		abs_file_ticks += delta;

		uint8_t status;
		UINT br;
		f_read(fil, &status, 1, &br);
		if (br == 0) break;
		bytes_processed++;

		if (status < MIDI_STATUS_VOICE_MIN) {
			status = last_status;
			f_lseek(fil, f_tell(fil) - 1);
			bytes_processed--;
		} else {
			last_status = status;
		}

		if (status == MIDI_STATUS_META) { 
			uint8_t type;
			f_read(fil, &type, 1, &br);
			if (br == 0) break;
			bytes_processed++;
			uint32_t len_bytes = 0;
			start_pos = f_tell(fil);
			uint32_t len = read_vlq(fil, &len_bytes);
			bytes_processed += (f_tell(fil) - start_pos);
			skip_bytes(fil, len);
			bytes_processed += len;
		}
		else if (status == MIDI_STATUS_SYSEX || status == MIDI_STATUS_SYSEX_END) {
			uint32_t len_bytes = 0;
			start_pos = f_tell(fil);
			uint32_t len = read_vlq(fil, &len_bytes);
			bytes_processed += (f_tell(fil) - start_pos);
			skip_bytes(fil, len);
			bytes_processed += len;
		}
		else if ((status & MIDI_STATUS_SYSEX) >= MIDI_STATUS_VOICE_MIN && (status & MIDI_STATUS_SYSEX) <= MIDI_STATUS_VOICE_MAX) {
			MidiEvent *ev = &track->events[ev_idx];
			
			uint32_t abs_internal = (uint32_t)(((uint64_t)abs_file_ticks * INTERNAL_PPQN) / file_ppqn);
			
			ev->step = abs_internal / TICKS_PER_STEP;
			uint32_t remainder = abs_internal % TICKS_PER_STEP;
			ev->micro_delay = (remainder * MAX_MICRO_DELAY) / TICKS_PER_STEP;
			
			ev->status = status;
			
			f_read(fil, &ev->data1, 1, &br);
			if (br == 0) break;
			bytes_processed++;
			
			if ((status & MIDI_STATUS_VOICE_MAX) == MIDI_STATUS_PROG_CHG || (status & MIDI_STATUS_SYSEX) == MIDI_STATUS_CH_AT) {
				ev->data2 = 0; 
			} else {
				f_read(fil, &ev->data2, 1, &br);
				if (br == 0) break;
				bytes_processed++;
			}
			
			ev_idx++;
		}
	}
	track->event_count = ev_idx;
	
	if (track->event_count > 0) {
		qsort(track->events, track->event_count, sizeof(MidiEvent), compare_events);
	}
}
bool storage_init(void) {
	if (fs_mounted) return true;
	FRESULT fr = f_mount(&fs, "0:", 1);

	if (fr != FR_OK) return false;
	fs_mounted = true;

	for (int i = 0; i < MAX_LOADED_SONGS; i++) {
		event_pool_used[i] = 0;
		ring_song_ids[i] = EMPTY_SLOT_ID;
	}
	return true;
}

int storage_scan_folders(const char* path, char out_folder_list[][MAX_FILE_NAME_LEN], int max_folders) {
	FRESULT fr;
	DIR dir;
	FILINFO fno;
	int folder_count = 0;

	fr = f_opendir(&dir, path);
	if (fr != FR_OK) return 0;

	while (folder_count < max_folders) {
		fr = f_readdir(&dir, &fno);
		if (fr != FR_OK || fno.fname[0] == 0) break;
		if (fno.fname[0] == '.') continue;
		
		if (fno.fattrib & AM_DIR) {
			strncpy(out_folder_list[folder_count], fno.fname, MAX_FILE_NAME_LEN - 1);
			out_folder_list[folder_count][MAX_FILE_NAME_LEN - 1] = '\0';
			folder_count++;
		}
	}
	f_closedir(&dir);
	return folder_count;
}

void storage_free_song(Song* song) {
	if (!song) return;
	
	int slot = song - loaded_songs;
	if (slot >= 0 && slot < MAX_LOADED_SONGS) {
		event_pool_used[slot] = 0;
	}

	for (int i = 0; i < MAX_TRACKS; i++) {
		song->tracks[i].events = NULL;
		song->tracks[i].event_count = 0;
		song->tracks[i].capacity = 0;
		song->tracks[i].is_modified = false;
	}
}

static int parse_hex_prefix(const char* name) {
	if (!name[0] || !name[1]) return INVALID_HEX_ID;
	int val = 0;
	for (int i = 0; i < HEX_PREFIX_LEN; i++) {
		char c = name[i];
		val <<= HEX_SHIFT_BITS;
		if (c >= '0' && c <= '9') val |= (c - '0');
		else if (c >= 'A' && c <= 'F') val |= (c - 'A' + HEX_ALPHA_OFFSET);
		else if (c >= 'a' && c <= 'f') val |= (c - 'a' + HEX_ALPHA_OFFSET);
		else return INVALID_HEX_ID;
	}
	return val;
}

static bool get_song_path(const char* project_path, uint8_t song_id, char* out_path) {
	DIR dir;
	FILINFO fno;
	bool found = false;

	if (f_opendir(&dir, project_path) == FR_OK) {
		while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
			if (fno.fattrib & AM_DIR) {
				if (fno.fname[0] == '.') continue;
				int parsed_id = parse_hex_prefix(fno.fname);
				if (parsed_id == song_id) {
					snprintf(out_path, MAX_PATH_LEN, PATH_FMT_SCAN, project_path, fno.fname);
					found = true;
					break;
				}
			}
		}
		f_closedir(&dir);
	}
	
	if (!found) {
		snprintf(out_path, MAX_PATH_LEN, PATH_FMT_NEW, project_path, song_id);
	}
	
	return found;
}

bool storage_load_song(const char* project_path, uint8_t song_id, Song* song) {
	storage_free_song(song);
	memset(song, 0, sizeof(Song));
	song->tempo = DEFAULT_TEMPO_BPM;

	char song_path[MAX_PATH_LEN];
	if (!get_song_path(project_path, song_id, song_path)) {
		if (f_mkdir(song_path) == FR_OK) {
			return true;
		}
		return false;
	}

	DIR dir;
	FILINFO fno;
	if (f_opendir(&dir, song_path) != FR_OK) return false;

	while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
		if (fno.fattrib & AM_DIR) continue;
		if (fno.fname[0] == '.') continue;

		char *ext = strrchr(fno.fname, '.');
		if (!ext) continue;
		if (strcmp(ext, FILE_EXT_LOWER) != 0 && strcmp(ext, FILE_EXT_UPPER) != 0) continue;

		int target_idx = -1;
		bool is_config = false;

		if (strcmp(fno.fname, STR_CONFIG_FILE) == 0 || strcmp(fno.fname, STR_CONFIG_FILE_UPPER) == 0) {
			target_idx = CONFIG_TRACK_INDEX;
			is_config = true;
		} else {
			int num = atoi(fno.fname);
			if (num >= MIN_MUSIC_TRACK_NUM && num <= MAX_MUSIC_TRACK_NUM) {
				target_idx = num - 1;
			}
		}

		if (target_idx >= 0) {
			char filepath[MAX_PATH_LEN];
			snprintf(filepath, MAX_PATH_LEN, PATH_FMT_SCAN, song_path, fno.fname);

			FIL fil;
			if (f_open(&fil, filepath, FA_READ) == FR_OK) {
				char chunk_id[CHUNK_ID_LEN + 1] = {0};
				UINT br;
				f_read(&fil, chunk_id, CHUNK_ID_LEN, &br);

				if (strncmp(chunk_id, CHUNK_ID_MTHD, CHUNK_ID_LEN) == 0) {
					uint32_t header_len = read_be32(&fil);
					uint16_t format = read_be16(&fil);
					uint16_t num_tracks = read_be16(&fil);
					uint16_t ppqn = read_be16(&fil);

					if (num_tracks > 0) {
						if (header_len > MTHD_DATA_LEN) skip_bytes(&fil, header_len - MTHD_DATA_LEN);

						while (true) {
							f_read(&fil, chunk_id, CHUNK_ID_LEN, &br);
							if (br < CHUNK_ID_LEN) break;
							uint32_t chunk_len = read_be32(&fil);

							if (strncmp(chunk_id, CHUNK_ID_MTRK, CHUNK_ID_LEN) == 0) {
								DWORD chunk_start = f_tell(&fil);
								uint32_t ev_count = 0;
								
								track_alloc_ram(&fil, chunk_len, &ev_count, is_config ? &song->tempo : NULL);

								Track *t = &song->tracks[target_idx];
								if (ev_count > 0) {
									int slot = song - loaded_songs;
									if (slot >= 0 && slot < MAX_LOADED_SONGS) {
										if ((event_pool_used[slot] + ev_count) <= MAX_EVENTS_PER_SONG) {
											t->events = &event_pool[slot][event_pool_used[slot]];
											event_pool_used[slot] += ev_count;
											t->capacity = ev_count;
											f_lseek(&fil, chunk_start);
											track_load_data(&fil, chunk_len, t, ppqn);
										}
									}
								}
								t->is_modified = false;
								break; 
							} else {
								skip_bytes(&fil, chunk_len);
							}
						}
					}
				}
				f_close(&fil);
			}
		}
	}
	f_closedir(&dir);
	return true;
}

bool storage_save_song(const char* project_path, uint8_t song_id, Song* song) {
	char song_path[MAX_PATH_LEN];
	get_song_path(project_path, song_id, song_path);
	f_mkdir(song_path);

	for (int i = 0; i < MAX_TRACKS; i++) {
		Track *t = &song->tracks[i];
		
		if (!t->is_modified) continue;

		bool is_config = (i == CONFIG_TRACK_INDEX);
		char filename[MAX_FILE_NAME_LEN];
		
		if (is_config) {
			snprintf(filename, sizeof(filename), "%s", STR_CONFIG_FILE);
		} else {
			snprintf(filename, sizeof(filename), "%d%s", i + 1, FILE_EXT_LOWER);
		}

		char filepath[MAX_PATH_LEN];
		snprintf(filepath, MAX_PATH_LEN, PATH_FMT_SCAN, song_path, filename);

		FIL fil;
		if (f_open(&fil, filepath, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) continue;

		UINT bw;
		f_write(&fil, CHUNK_ID_MTHD, CHUNK_ID_LEN, &bw);
		write_be32(&fil, MTHD_DATA_LEN);
		write_be16(&fil, MTHD_FORMAT_0); 
		write_be16(&fil, MTHD_ONE_TRACK); 
		write_be16(&fil, INTERNAL_PPQN);

		if (t->event_count > 0) {
			qsort(t->events, t->event_count, sizeof(MidiEvent), compare_events);
		}

		f_write(&fil, CHUNK_ID_MTRK, CHUNK_ID_LEN, &bw);
		
		DWORD len_pos = f_tell(&fil);
		write_be32(&fil, 0); 
		DWORD chunk_start = f_tell(&fil);

		if (is_config && song->tempo > 0) {
			write_vlq(&fil, 0);
			uint8_t tempo_header[2] = {MIDI_STATUS_META, META_TYPE_TEMPO};
			f_write(&fil, tempo_header, 2, &bw);
			write_vlq(&fil, META_LEN_TEMPO);
			
			uint32_t us_per_qn = MICROSECONDS_PER_MINUTE / song->tempo;
			uint8_t temp[META_LEN_TEMPO] = { (us_per_qn >> 16) & 0xFF, (us_per_qn >> 8) & 0xFF, us_per_qn & 0xFF };
			f_write(&fil, temp, META_LEN_TEMPO, &bw);
		}

		uint32_t last_tick = 0;
		
		for (uint32_t j = 0; j < t->event_count; j++) {
			MidiEvent *ev = &t->events[j];
			
			uint32_t current_tick = (ev->step * TICKS_PER_STEP) + 
								  ((ev->micro_delay * TICKS_PER_STEP) / MAX_MICRO_DELAY);
			
			if (current_tick < last_tick) current_tick = last_tick;
			
			uint32_t delta = current_tick - last_tick;
			write_vlq(&fil, delta);
			last_tick = current_tick;
			
			f_write(&fil, &ev->status, 1, &bw);
			f_write(&fil, &ev->data1, 1, &bw);
			
			if ((ev->status & MIDI_STATUS_VOICE_MAX) != MIDI_STATUS_PROG_CHG && (ev->status & MIDI_STATUS_SYSEX) != MIDI_STATUS_CH_AT) {
				f_write(&fil, &ev->data2, 1, &bw);
			}
		}

		write_vlq(&fil, 0);
		uint8_t eot_seq[3] = {MIDI_STATUS_META, META_TYPE_EOT, META_LEN_EOT};
		f_write(&fil, eot_seq, 3, &bw);

		DWORD chunk_end = f_tell(&fil);
		uint32_t chunk_len = chunk_end - chunk_start;
		f_lseek(&fil, len_pos);
		write_be32(&fil, chunk_len);
		f_lseek(&fil, chunk_end);

		f_close(&fil);
		
		t->is_modified = false;
	}

	return true;
}


void storage_set_project(const char* project_name) {
	strncpy(active_projects[PREV_PROJECT_IDX], active_projects[CURRENT_PROJECT_IDX], MAX_PATH_LEN - 1);
	active_projects[PREV_PROJECT_IDX][MAX_PATH_LEN - 1] = '\0';
	strncpy(active_projects[CURRENT_PROJECT_IDX], project_name, MAX_PATH_LEN - 1);
	active_projects[CURRENT_PROJECT_IDX][MAX_PATH_LEN - 1] = '\0';
}

bool event_storage_load(uint8_t song_id, bool previous_project) {
	uint8_t target_proj = previous_project ? PREV_PROJECT_IDX : CURRENT_PROJECT_IDX;
	
	for (int i = 0; i < MAX_LOADED_SONGS; i++) {
		if (ring_song_ids[i] == song_id && loaded_songs[i].project_index == target_proj) {
			for (int j = 0; j < MAX_LOADED_SONGS; j++) {
				if (ring_song_ids[j] != EMPTY_SLOT_ID) {
					storage_save_song(active_projects[loaded_songs[j].project_index], ring_song_ids[j], &loaded_songs[j]);
				}
			}
			return true;
		}
	}

	if (ring_song_ids[ring_idx] != EMPTY_SLOT_ID) {
		storage_save_song(active_projects[loaded_songs[ring_idx].project_index], ring_song_ids[ring_idx], &loaded_songs[ring_idx]);
		storage_free_song(&loaded_songs[ring_idx]);
	}

	bool success = storage_load_song(active_projects[target_proj], song_id, &loaded_songs[ring_idx]);
	
	if (success) {
		loaded_songs[ring_idx].project_index = target_proj;
		ring_song_ids[ring_idx] = song_id;
		ring_idx = (ring_idx + 1) % MAX_LOADED_SONGS;
	} else {
		ring_song_ids[ring_idx] = EMPTY_SLOT_ID;
	}

	return success;
}
