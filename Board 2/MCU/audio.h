// audio.h
// Plays 8-bit unsigned mono samples through the DAC (PA5 or PA4, see
// AUDIO_DAC_CHANNEL below).
//
// How it works:
//   TIM6 overflows once per sample (8 kHz) and triggers the DAC.
//   Each trigger, the DAC asks DMA1 channel 4 for the next byte.
//   The CPU only starts a sound and gets one interrupt when it ends.
//
// Sample format (what Audacity exports as "Unsigned 8-bit PCM"):
//   0 = most negative, 128 = silence, 255 = most positive.
//   The DAC turns that into 0 V .. 3.3 V, so silence sits at about 1.65 V.
//   AC-couple the output into the LM386 and keep the input level small.

#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stm32l432xx.h>

#define AUDIO_SAMPLE_RATE 8000   // Hz; must match the rate of the sound arrays

// Which DAC output to use:
//   2 = PA5. The NUCLEO-L432KC's SB18 also ties PA5 to PB7, so leave PB7
//       unconnected.
//   1 = PA4. Also wired to the SW2 push button; don't press SW2.
#define AUDIO_DAC_CHANNEL 2

// 1 = DMA feeds the DAC (CPU free while playing).
// 0 = no DMA: audioService() in the main loop copies each sample into the DAC
//     on every TIM6 tick. Use this to check whether DMA is the problem.
#define AUDIO_USE_DMA 1

// Set up the DAC pin, DAC, TIM6 and DMA. Call after configureClock().
void audioInit(void);

// Start playing `length` samples from `samples`. Returns right away.
// If a sound is already playing, it stops and the new one starts.
void audioPlay(const uint8_t *samples, uint32_t length);

// 1 while a sound is playing.
int audioBusy(void);

// Call this every pass of the main loop. Does the playback when
// AUDIO_USE_DMA is 0; does nothing when it is 1.
void audioService(void);

// Debug: play a 500 Hz square wave for `ms` milliseconds by writing the DAC
// from the CPU (no DMA). Blocks until done.
// If this is audible but audioPlay() is not, the DAC, wiring and amp are fine
// and the problem is in the DMA path (try AUDIO_USE_DMA 0).
void audioTestTone(uint32_t ms);

#endif
