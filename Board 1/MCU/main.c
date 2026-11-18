// main.c
// Laser tag MCU: plays a sound when this unit fires or gets hit, keeps the
// game state (game.c) and serves the scoreboard page through the ESP8266
// (web.c). The FPGA reports shots and hits on their own interrupt lines.
//
// A game is started and reset from the scoreboard page. Until a game is live
// the blaster is disarmed and hits are ignored.
//
// Pin plan (E155 FA23 Development Board v4, SW7 = MCU <-> FPGA bridges):
//   SW7 1  PA10 <-> P23   USART1 RX  (ESP8266)         ON or OFF (FPGA ignores P23)
//   SW7 2  PA9  <-> P25   USART1 TX  (ESP8266)         ON or OFF (FPGA ignores P25)
//   SW7 3  PA5  <-> P26   DAC audio out                OFF
//   SW7 4  PA6  <-  P27   HIT interrupt (lights D4)    ON
//   SW7 5  PA11  -> P20   SPI CS                       ON
//   SW7 6  PB5   -> P10   SPI MOSI                     ON
//   SW7 7  PB4  <-  P12   SPI MISO                     ON
//   SW7 8  PB3   -> P21   SPI SCK                      ON
//   PB0  <- FPGA P18      FIRE interrupt               jumper wire
//   PA5  -> DAC audio  -> 10 uF -> volume pot -> LM386 pin 3
//   PA9  -> USART1 TX, PA10 <- USART1 RX, 125000 baud to the ESP8266 (E155
//           course web server firmware; TX and RX cross over)
//   PA7  <- SW3 push button: plays the hit sound (speaker test)
//   PB6, PB7: leave unconnected (Nucleo solder bridges tie them to PA6, PA5)
//
// FIRE (PB0) rising edge: this unit just fired -> shot sound.
// HIT  (PA6) rising edge: this unit got hit    -> hit sound.
// The FPGA decides what counts as a hit: only the other player's shots, or
// every shot (including your own) when SW6 switch 1 is ON (test mode).
//
// Which sound wins: a shot always plays right away. A hit that arrives while
// the shot sound is playing waits for it to finish.

#include "STM32L432KC.h"
#include "audio.h"
#include "fpga.h"
#include "game.h"
#include "web.h"
#include "hit_sound.h"
#include "shot_sound.h"

// 1 = play tune.h over and over (speaker/amp test).
#define PLAY_TUNE_ON_REPEAT 0

// 1 = standalone test: no Wi-Fi/scoreboard, no game, no lives. The blaster
//     is always armed, every shot plays the shot sound and every hit plays
//     the hit sound. Set back to 0 for the normal game with the scoreboard.
#define STANDALONE_TEST 1

#if HIT_SOUND_RATE != AUDIO_SAMPLE_RATE
#error "hit_sound.h sample rate doesn't match AUDIO_SAMPLE_RATE; re-export at 8000 Hz"
#endif
#if SHOT_SOUND_RATE != AUDIO_SAMPLE_RATE
#error "shot_sound.h sample rate doesn't match AUDIO_SAMPLE_RATE; re-export at 8000 Hz"
#endif

#if PLAY_TUNE_ON_REPEAT
#include "tune.h"   // 110 KB of flash, so only pulled in when used
#endif

#define TEST_BTN_PIN  PA7
#define BAUD_RATE     125000   // fixed by the ESP8266 course firmware

///////////////////////////////////////////////////////////////////////////////
// 1 ms tick (SysTick) for debouncing and, later, game timing
///////////////////////////////////////////////////////////////////////////////

static volatile uint32_t ms_ticks = 0;

void SysTick_Handler(void) {
    ms_ticks++;
}

int main(void) {
    configureFlash();
    configureClock();       // 80 MHz from PLL
    SysTick_Config(SystemCoreClock / 1000);

#if !STANDALONE_TEST
    // The serial port belongs to the ESP8266: only web.c sends on it.
    USART_TypeDef *uart = initUSART(USART1_ID, BAUD_RATE);
    uartEnableRxInterrupt();
    webInit(uart);
#endif

    audioInit();
    fpgaInit();             // interrupts on PA6/PB0; SPI idle (CS high)
#if STANDALONE_TEST
    fpgaSetArmed(1);        // always armed, no game
#else
    gameInit();             // idle, blaster disarmed until a game goes live
#endif

    pinMode(TEST_BTN_PIN, GPIO_INPUT);
    pinResistor(TEST_BTN_PIN, GPIO_PULL_UP);

    __enable_irq();

    audioTestTone(150);     // short beep: MCU is running and the speaker works

    // What is playing right now (so a sound is never restarted over itself)
    enum { SND_NONE, SND_SHOT, SND_HIT } playing = SND_NONE;
    int hit_pending = 0;    // hit sound waiting for the shot sound to end

    int      btn_stable = 1, btn_last = 1;
    uint32_t btn_changed_ms = 0;

    while (1) {
        audioService();

#if !STANDALONE_TEST
        // ---- Scoreboard page requests and game timing ----
        webService(ms_ticks);
        gameTick(ms_ticks);
#endif

#if PLAY_TUNE_ON_REPEAT
        if (!audioBusy()) audioPlay(tune, TUNE_LENGTH);
#endif

        if (!audioBusy()) playing = SND_NONE;

        // ---- This unit fired (FIRE pin) ----
        if (fpgaFirePending()) {
            audioPlay(shot_sound, SHOT_SOUND_LENGTH);
            playing = SND_SHOT;
        }

        // ---- This unit got hit (HIT pin) ----
        // Only counts while a game is live. The page picks up the new lives
        // count the next time it asks for the status.
        if (fpgaHitPending()) {
#if STANDALONE_TEST
            hit_pending = 1;    // every hit plays the sound, no lives
#else
            if (gameHit(ms_ticks)) hit_pending = 1;
#endif
        }

        // Play a waiting hit once the shot sound is done. If the hit sound is
        // already playing, let it finish instead of restarting it.
        if (hit_pending && playing != SND_SHOT) {
            hit_pending = 0;
            if (playing != SND_HIT) {
                audioPlay(hit_sound, HIT_SOUND_LENGTH);
                playing = SND_HIT;
            }
        }

        // ---- SW3: play the hit sound (speaker test), 30 ms debounce ----
        int btn = digitalRead(TEST_BTN_PIN);
        if (btn != btn_last) {
            btn_last = btn;
            btn_changed_ms = ms_ticks;
        } else if (btn != btn_stable && (ms_ticks - btn_changed_ms) >= 30) {
            btn_stable = btn;
            if (!btn) {             // pressed
                audioPlay(hit_sound, HIT_SOUND_LENGTH);
                playing = SND_HIT;
            }
        }
    }
}
