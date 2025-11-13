/**
 * Sequential segment driver for CH32V003
 * Driven via EXTI interrupt on PC[INTERRUPT_PIN] (both edges)
 * Cycles through "SEGMENTS_TOTAL"" total segments in blocks of "SEGMENTS_ON_BOARD" per board
 */

#include "../../CH32V003_TRUE_MINIMAL/ch32v003.h"
#include <stdint.h>

#ifndef BLOCK_NUMBER
#define BLOCK_NUMBER (0)
#endif

#define SEGMENTS_ON_BOARD (16)
#define SEGMENTS_TOTAL (96)

#define INTERRUPT_PIN (5)
#define FALLING_EDGE_ENABLE (1)
#define RISING_EDGE_ENABLE (1)

#define RES_OFFSET ((uint8_t)(0x10))

#define SEGMENTS_ON_BOARD_LAST_RESET (SEGMENTS_ON_BOARD + 1)
#define DO_NOTHING_BSHR (0)

typedef struct {
    volatile uint32_t* setPort;
    volatile uint32_t setPin;
    volatile uint32_t* resetPort;
    volatile uint32_t resetPin;
} preperedBSHR_t;


static const preperedBSHR_t gpioSeq[SEGMENTS_ON_BOARD_LAST_RESET] = {
  { .setPort = &GPIOD_BSHR, .setPin = PIN(5), .resetPort = &GPIOC_BSHR, .resetPin = DO_NOTHING_BSHR  },
  { .setPort = &GPIOA_BSHR, .setPin = PIN(1), .resetPort = &GPIOD_BSHR, .resetPin = PIN(5) << RES_OFFSET },
  { .setPort = &GPIOD_BSHR, .setPin = PIN(0), .resetPort = &GPIOA_BSHR, .resetPin = PIN(1) << RES_OFFSET },
  { .setPort = &GPIOC_BSHR, .setPin = PIN(0), .resetPort = &GPIOD_BSHR, .resetPin = PIN(0) << RES_OFFSET },
  { .setPort = &GPIOD_BSHR, .setPin = PIN(6), .resetPort = &GPIOC_BSHR, .resetPin = PIN(0) << RES_OFFSET },
  { .setPort = &GPIOD_BSHR, .setPin = PIN(7), .resetPort = &GPIOD_BSHR, .resetPin = PIN(6) << RES_OFFSET },
  { .setPort = &GPIOA_BSHR, .setPin = PIN(2), .resetPort = &GPIOD_BSHR, .resetPin = PIN(7) << RES_OFFSET },
  { .setPort = &GPIOC_BSHR, .setPin = PIN(1), .resetPort = &GPIOA_BSHR, .resetPin = PIN(2) << RES_OFFSET },
  { .setPort = &GPIOD_BSHR, .setPin = PIN(4), .resetPort = &GPIOC_BSHR, .resetPin = PIN(1) << RES_OFFSET },
  { .setPort = &GPIOD_BSHR, .setPin = PIN(1), .resetPort = &GPIOD_BSHR, .resetPin = PIN(4) << RES_OFFSET },
  { .setPort = &GPIOC_BSHR, .setPin = PIN(4), .resetPort = &GPIOD_BSHR, .resetPin = PIN(1) << RES_OFFSET },
  { .setPort = &GPIOC_BSHR, .setPin = PIN(2), .resetPort = &GPIOC_BSHR, .resetPin = PIN(4) << RES_OFFSET },
  { .setPort = &GPIOD_BSHR, .setPin = PIN(3), .resetPort = &GPIOC_BSHR, .resetPin = PIN(2) << RES_OFFSET },
  { .setPort = &GPIOD_BSHR, .setPin = PIN(2), .resetPort = &GPIOD_BSHR, .resetPin = PIN(3) << RES_OFFSET },
  { .setPort = &GPIOC_BSHR, .setPin = PIN(7), .resetPort = &GPIOD_BSHR, .resetPin = PIN(2) << RES_OFFSET },
  { .setPort = &GPIOC_BSHR, .setPin = PIN(3), .resetPort = &GPIOC_BSHR, .resetPin = PIN(7) << RES_OFFSET },
  { .setPort = &GPIOC_BSHR, .setPin = DO_NOTHING_BSHR , .resetPort = &GPIOC_BSHR, .resetPin = PIN(3) << RES_OFFSET }
};

volatile uint8_t segmentCtr;

// ISR triggered on both edges of PC5.
// Advances segment sequence and updates GPIO outputs atomically.
void EXTI7_0_IRQHandler(void) __attribute__((interrupt("machine")));
void EXTI7_0_IRQHandler(void)
{
  register const preperedBSHR_t* seq = &gpioSeq[segmentCtr];

    if (segmentCtr < SEGMENTS_ON_BOARD) {
      *(seq->resetPort) = seq->resetPin;
      *(seq->setPort)   = seq->setPin;
    } else if (segmentCtr == SEGMENTS_ON_BOARD) {
      *(seq->resetPort) = seq->resetPin;
    }
    segmentCtr++;

    if (segmentCtr >= SEGMENTS_TOTAL){
      segmentCtr = 0;
    }
    EXTI_CLEAR(INTERRUPT_PIN);
}



int main(void)
{
    RCC_ENABLE_ALL_GPIO();
    RCC_APB2PCENR |= RCC_AFIOEN;

    GPIOA_CONFIG(0x07, GPIO_MODE_OUT_PP_50MHZ);
    GPIOC_CONFIG(0x9F, GPIO_MODE_OUT_PP_50MHZ);
    GPIOD_CONFIG(0xFF, GPIO_MODE_OUT_PP_50MHZ);

    GPIOC_CONFIG(PIN(INTERRUPT_PIN), GPIO_MODE_IN_PUPD);
    GPIOC_BSHR = PIN(INTERRUPT_PIN); // Pull-up

    segmentCtr = BLOCK_NUMBER * SEGMENTS_ON_BOARD;

    SETUP_EXTI_PIN(INTERRUPT_PIN, AFIO_EXTICR_PC, FALLING_EDGE_ENABLE , RISING_EDGE_ENABLE , EXTI7_0_IRQn);
    ENABLE_GLOBAL_IRQS();

    while (1) {
    }
}
