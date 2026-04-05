#ifndef SD_DRIVER_H
#define SD_DRIVER_H

#include <stdbool.h>

bool sd_driver_init(void);
bool sd_driver_is_mounted(void);

#endif
