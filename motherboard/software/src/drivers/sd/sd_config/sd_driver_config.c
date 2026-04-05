#include <string.h>
#include "hw_config.h"

static sd_card_t sd_cards[] = {
    {
        .type = SD_IF_SDIO,
        .sdio_if_p = (sd_sdio_if_t[]){
            {
                .CMD_gpio = 18,
                .D0_gpio = 19,
                .CLK_gpio = 24,
                .SDIO_PIO = pio0,
                .baud_rate = 10 * 1000 * 1000
            }
        },
        .use_card_detect = true,
        .card_detect_gpio = 23,
        .card_detected_true = 0
    }
};

size_t sd_get_num() { return count_of(sd_cards); }
sd_card_t *sd_get_by_num(size_t num) { return &sd_cards[num]; }
