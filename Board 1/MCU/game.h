// game.h
// Game state for one laser tag unit: phase, lives and hit count.
//
// The scoreboard page starts and resets the game (see web.c). A game begins
// with a 3 second countdown, then goes live. Each hit while live costs one
// life; at zero lives the game is over and this unit has lost.
//
// The blaster is armed (over SPI, see fpga.h) only while the game is live.

#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#define GAME_LIVES        3
#define GAME_COUNTDOWN_MS 3000

// The numbers are what the scoreboard page receives.
typedef enum {
    GAME_IDLE      = 0,   // waiting for Start
    GAME_COUNTDOWN = 1,   // 3, 2, 1...
    GAME_LIVE      = 2,   // hits count, blaster armed
    GAME_OVER      = 3    // this unit ran out of lives
} game_phase_t;

// Call once after fpgaInit(). Disarms the blaster.
void gameInit(void);

// `now` is the 1 ms tick count in all of these.
void gameStart(uint32_t now);   // begin the countdown (ignored mid-game)
void gameReset(void);           // back to idle with full lives
void gameTick(uint32_t now);    // call every main-loop pass

// Call when the FPGA reports a hit. Returns 1 if the hit counted (game live).
int gameHit(uint32_t now);

game_phase_t gamePhase(void);
int          gameLives(void);
int          gameHits(void);
int          gamePlayer(void);              // 1 or 2 (from the FPGA MY_ID: 1010 = 1, 1100 = 2)
uint32_t     gameElapsedMs(uint32_t now);   // time since the game went live

#endif
