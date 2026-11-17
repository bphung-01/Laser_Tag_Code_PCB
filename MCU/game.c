// game.c
// Game state for one laser tag unit. See game.h.

#include "game.h"
#include "fpga.h"

static game_phase_t phase = GAME_IDLE;
static int      lives  = GAME_LIVES;
static int      hits   = 0;
static int      player = 1;
static uint32_t phase_start_ms = 0;   // when the countdown / live phase began
static uint32_t final_ms = 0;         // match length, frozen at game over

static void readPlayer(void) {
    player = (fpgaReadStatus() & FPGA_STATUS_ME_P2) ? 2 : 1;
}

void gameInit(void) {
    readPlayer();
    gameReset();
}

void gameReset(void) {
    phase    = GAME_IDLE;
    lives    = GAME_LIVES;
    hits     = 0;
    final_ms = 0;
    fpgaSetArmed(0);
}

void gameStart(uint32_t now) {
    if (phase == GAME_COUNTDOWN || phase == GAME_LIVE) return;
    gameReset();
    readPlayer();               // in case SW6 was changed since power-up
    phase = GAME_COUNTDOWN;
    phase_start_ms = now;
}

void gameTick(uint32_t now) {
    if (phase == GAME_COUNTDOWN && (now - phase_start_ms) >= GAME_COUNTDOWN_MS) {
        phase = GAME_LIVE;
        phase_start_ms = now;
        fpgaSetArmed(1);
    }
}

int gameHit(uint32_t now) {
    if (phase != GAME_LIVE) return 0;

    hits++;
    if (lives > 0) lives--;

    if (lives == 0) {
        phase    = GAME_OVER;
        final_ms = now - phase_start_ms;
        fpgaSetArmed(0);
        // Later: play the "you lose" sound here.
    }
    return 1;
}

game_phase_t gamePhase(void)  { return phase; }
int          gameLives(void)  { return lives; }
int          gameHits(void)   { return hits; }
int          gamePlayer(void) { return player; }

uint32_t gameElapsedMs(uint32_t now) {
    if (phase == GAME_LIVE) return now - phase_start_ms;
    if (phase == GAME_OVER) return final_ms;
    return 0;
}
