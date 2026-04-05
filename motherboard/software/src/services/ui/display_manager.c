#include "display_manager.h"
#include "../../drivers/display/display_driver.h"
#include "pico/stdlib.h"

#define SEG_A (1 << 0)
#define SEG_B (1 << 1)
#define SEG_C (1 << 2)
#define SEG_D (1 << 3)
#define SEG_E (1 << 4)
#define SEG_F (1 << 5)
#define SEG_G (1 << 6)
#define DELIMETER_MASK (0x80)
#define CHAR_MASK (0x7F)
#define BLIND_TIME (10)

static const uint8_t segmap[128] = {
    ['0'] = SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F,
    ['1'] = SEG_B|SEG_C,
    ['2'] = SEG_A|SEG_B|SEG_D|SEG_E|SEG_G,
    ['3'] = SEG_A|SEG_B|SEG_C|SEG_D|SEG_G,
    ['4'] = SEG_B|SEG_C|SEG_F|SEG_G,
    ['5'] = SEG_A|SEG_C|SEG_D|SEG_F|SEG_G,
    ['6'] = SEG_A|SEG_C|SEG_D|SEG_E|SEG_F|SEG_G,
    ['7'] = SEG_A|SEG_B|SEG_C,
    ['8'] = SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F|SEG_G,
    ['9'] = SEG_A|SEG_B|SEG_C|SEG_D|SEG_F|SEG_G,
    ['A'] = SEG_A|SEG_B|SEG_C|SEG_E|SEG_F|SEG_G,
    ['B'] = SEG_C|SEG_D|SEG_E|SEG_F|SEG_G,
    ['C'] = SEG_A|SEG_D|SEG_E|SEG_F,
    ['D'] = SEG_B|SEG_C|SEG_D|SEG_E|SEG_G,
    ['E'] = SEG_A|SEG_D|SEG_E|SEG_F|SEG_G,
    ['F'] = SEG_A|SEG_E|SEG_F|SEG_G,
    ['G'] = SEG_A|SEG_C|SEG_D|SEG_E|SEG_F,
    ['H'] = SEG_B|SEG_C|SEG_E|SEG_F|SEG_G,
    ['I'] = SEG_B|SEG_C,
    ['J'] = SEG_B|SEG_C|SEG_D|SEG_E,
    ['L'] = SEG_D|SEG_E|SEG_F,
    ['O'] = SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F,
    ['P'] = SEG_A|SEG_B|SEG_E|SEG_F|SEG_G,
    ['S'] = SEG_A|SEG_C|SEG_D|SEG_F|SEG_G,
    ['U'] = SEG_B|SEG_C|SEG_D|SEG_E|SEG_F,
    [' '] = 0
};

static uint8_t to7seg(char c) { return (unsigned char)c < 128 ? segmap[(int)c] : 0; }

void display_manager_init(void) { display_driver_init(); }

void display_manager_update(char left, char right) {
    display_driver_clear();
    display_driver_next_digit();
    sleep_us(BLIND_TIME);
    display_driver_set_delimiters(left & DELIMETER_MASK, right & DELIMETER_MASK);
    display_driver_set_segments(to7seg(left & CHAR_MASK), to7seg(right & CHAR_MASK));
}

void display_manager_clear(void) { display_driver_clear(); }
