#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <stdint.h>

void display_manager_init(void);
void display_manager_update(char left, char right);
void display_manager_clear(void);

#endif
