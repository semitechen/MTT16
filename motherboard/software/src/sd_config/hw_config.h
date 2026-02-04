#ifndef HW_CONFIG_H
#define HW_CONFIG_H

#include "pico/stdlib.h"
#include "sd_card.h"

size_t sd_get_num();
sd_card_t *sd_get_by_num(size_t num);


#endif
