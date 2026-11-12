// main.c
// Laser tag MCU: play a sound when the player shoots or gets hit.
//
// Wiring (E155 FA23 Development Board v4):
//   PA6  <- FPGA P27 hit_irq   through SW7 switch 4 (turn ON). D4 lights on a hit.
//   PB4  <- FPGA P12 fire_irq  through SW7 switch 7 (turn ON).
//   PA5  -> DAC audio out      -> 10 uF -> volume pot -> LM386 pin 3
//                              SW7 switch 3 must be OFF (it ties PA5 to FPGA P26)
//   PA7  <- SW3 push button    press to play the hit sound without the FPGA
//   PB3  -> LD3 / D5 LED       blinks while the program runs
//                              (SW7 switch 8 should be OFF)
//
// The FPGA holds hit_irq high for 0.25 s after a good packet, and fire_irq
// high while it sends a packet. A rising edge on either one causes an EXTI
// interrupt, and the main loop plays the matching sound.
//
// Which sound wins: a shot always plays right away. A hit that arrives while
// the shot sound is playing waits for it to finish (when you shoot your own
// receiver, the hit comes ~14 ms after the shot, and you'd never hear the
// shot otherwise).

#include "STM32L432KC.h"
#include "audio.h"
#include "hit_sound.h"
#include "shot_sound.h"

// 1 = play tune.h over and over (speaker/amp test). Hits and SW3 still play
//     the hit sound; the tune starts again when it finishes.
// 0 = normal: silent until a hit.
#define PLAY_TUNE_ON_REPEAT 0

#if HIT_SOUND_RATE != AUDIO_SAMPLE_RATE
#error "hit_sound.h sample rate doesn't match AUDIO_SAMPLE_RATE; re-export at 8000 Hz"
#endif
#if SHOT_SOUND_RATE != AUDIO_SAMPLE_RATE
#error "shot_sound.h sample rate doesn't match AUDIO_SAMPLE_RATE; re-export at 8000 Hz"
#endif

#if PLAY_TUNE_ON_REPEAT
#include "tune.h"   // 110 KB of flash, so only pulled in when used
#if TUNE_RATE != AUDIO_SAMPLE_RATE
#error "tune.h sample rate doesn't match AUDIO_SAMPLE_RATE; re-export at 8000 Hz"
#endif
#endif

#define HIT_PIN       PA6
#define FIRE_PIN      PB4
#define TEST_BTN_PIN  PA7
#define HEARTBEAT_PIN PB3   // Nucleo green LED LD3 and dev board D5

static volatile int hit_pending  = 0;
static volatile int fire_pending = 0;

static void initFpgaInterrupts(void) {
    gpioEnable(GPIO_PORT_A);
    gpioEnable(GPIO_PORT_B);
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    // ---- Hit: PA6 -> EXTI line 6, rising edge ----
    pinMode(HIT_PIN, GPIO_INPUT);
    pinResistor(HIT_PIN, GPIO_PULL_DOWN);   // stays low if SW7 switch 4 is OFF
    SYSCFG->EXTICR[1] &= ~SYSCFG_EXTICR2_EXTI6;   // 000 = port A
    EXTI->RTSR1 |= EXTI_RTSR1_RT6;
    EXTI->FTSR1 &= ~EXTI_FTSR1_FT6;
    EXTI->PR1    = EXTI_PR1_PIF6;                 // clear anything stale
    EXTI->IMR1  |= EXTI_IMR1_IM6;

    // ---- Shot: PB4 -> EXTI line 4, rising edge ----
    pinMode(FIRE_PIN, GPIO_INPUT);
    pinResistor(FIRE_PIN, GPIO_PULL_DOWN);  // stays low if SW7 switch 7 is OFF
    SYSCFG->EXTICR[1] = (SYSCFG->EXTICR[1] & ~SYSCFG_EXTICR2_EXTI4)
                      | SYSCFG_EXTICR2_EXTI4_PB;  // 001 = port B
    EXTI->RTSR1 |= EXTI_RTSR1_RT4;
    EXTI->FTSR1 &= ~EXTI_FTSR1_FT4;
    EXTI->PR1    = EXTI_PR1_PIF4;
    EXTI->IMR1  |= EXTI_IMR1_IM4;

    NVIC_SetPriority(EXTI9_5_IRQn, 1);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
    NVIC_SetPriority(EXTI4_IRQn, 1);
    NVIC_EnableIRQ(EXTI4_IRQn);
}

void EXTI9_5_IRQHandler(void) {
    if (EXTI->PR1 & EXTI_PR1_PIF6) {
        EXTI->PR1 = EXTI_PR1_PIF6;   // write 1 to clear
        hit_pending = 1;
    }
}

void EXTI4_IRQHandler(void) {
    if (EXTI->PR1 & EXTI_PR1_PIF4) {
        EXTI->PR1 = EXTI_PR1_PIF4;   // write 1 to clear
        fire_pending = 1;
    }
}

int main(void) {
    // Turn the LED on first, so a lit LED with no blinking means the code is
    // stuck somewhere below (clock setup, or a fault / unnamed interrupt).
    gpioEnable(GPIO_PORT_B);
    pinMode(HEARTBEAT_PIN, GPIO_OUTPUT);
    digitalWrite(HEARTBEAT_PIN, PIO_HIGH);

    configureFlash();
    configureClock();       // 80 MHz from PLL

    audioInit();
    initFpgaInterrupts();

    pinMode(TEST_BTN_PIN, GPIO_INPUT);
    pinResistor(TEST_BTN_PIN, GPIO_PULL_UP);
    int btn_prev = 1;
    uint32_t loops = 0;
    int shot_playing = 0;   // 1 while the sound playing is the shot sound

    __enable_irq();

    while (1) {
        audioService();

#if PLAY_TUNE_ON_REPEAT
        if (!audioBusy()) {
            audioPlay(tune, TUNE_LENGTH);
        }
#endif

        if (!audioBusy()) shot_playing = 0;

        if (fire_pending) {
            fire_pending = 0;
            audioPlay(shot_sound, SHOT_SOUND_LENGTH);
            shot_playing = 1;
        }

        if (hit_pending && !shot_playing) {
            hit_pending = 0;
            // Later: read shooter ID over SPI and update lives here.
            audioPlay(hit_sound, HIT_SOUND_LENGTH);
        }

        // SW3 test button: play on press (simple edge detect, no debounce
        // needed since re-triggering just restarts the sound)
        int btn = digitalRead(TEST_BTN_PIN);
        if (btn_prev && !btn) {
            audioPlay(hit_sound, HIT_SOUND_LENGTH);
            shot_playing = 0;
        }
        btn_prev = btn;

        // Heartbeat: a few blinks per second
        if (++loops >= 300000) {
            loops = 0;
            togglePin(HEARTBEAT_PIN);
        }
    }
}
