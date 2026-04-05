#include "sd_driver.h"
#include "sd_config/hw_config.h"
#include "ff.h"

static FATFS fs;
static bool fs_mounted = false;

bool sd_driver_init(void) {
	if (fs_mounted) return true;
	sd_init_driver();
	FRESULT fr = f_mount(&fs, "0:", 1);
	if (fr != FR_OK) return false;
	fs_mounted = true;
	return true;
}

bool sd_driver_is_mounted(void) {
	return fs_mounted;
}
