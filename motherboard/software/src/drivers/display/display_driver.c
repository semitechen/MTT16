#include "display_driver.h"
#include "pico/stdlib.h"

#define displayInterruptPin (1)
#define DELIMETER_UP_PIN (6)
#define DELIMETER_LOW_PIN (5)

static const uint8_t segLeftPins[7] = {7, 11, 8, 2, 3, 13, 15};
static const uint8_t segRightPins[7] = {10, 9, 14, 17, 16, 4, 12};

static bool interruptFlipFlop = false;

void display_driver_init(void) {
    gpio_init(DELIMETER_UP_PIN);
    gpio_set_dir(DELIMETER_UP_PIN, GPIO_OUT);
    gpio_init(DELIMETER_LOW_PIN);
    gpio_set_dir(DELIMETER_LOW_PIN, GPIO_OUT);
    gpio_init(displayInterruptPin);
    gpio_set_dir(displayInterruptPin, GPIO_OUT);

    for (int i = 0; i < 7; i++) {
        gpio_init(segLeftPins[i]);
        gpio_set_dir(segLeftPins[i], GPIO_OUT);
        gpio_init(segRightPins[i]);
        gpio_set_dir(segRightPins[i], GPIO_OUT);
    }
}

void display_driver_set_segments(uint8_t left_mask, uint8_t right_mask) {
    for (int i = 0; i < 7; i++) {
        gpio_put(segLeftPins[i], (left_mask >> i) & 1);
        gpio_put(segRightPins[i], (right_mask >> i) & 1);
    }
}

void display_driver_set_delimiters(bool upper, bool lower) {
    gpio_put(DELIMETER_UP_PIN, upper);
    gpio_put(DELIMETER_LOW_PIN, lower);
}

void display_driver_clear(void) {
    display_driver_set_delimiters(false, false);
    display_driver_set_segments(0, 0);
}

void display_driver_next_digit(void) {
    gpio_put(displayInterruptPin, interruptFlipFlop);
    interruptFlipFlop = !interruptFlipFlop;
}
