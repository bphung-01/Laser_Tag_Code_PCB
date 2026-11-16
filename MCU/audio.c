// audio.c
// DAC + DMA sound playback. See audio.h.
//
// Resources used (AUDIO_DAC_CHANNEL in audio.h picks the pin):
//   Channel 2: PA5  DAC1_OUT2, DMA1 channel 4, request 5
//              SW7 switch 3 connects PA5 to FPGA P26. Keep that switch OFF.
//              NUCLEO-L432KC solder bridge SB18 ties PA5 to PB7, so leave
//              PB7 unconnected (the code makes PB7 high-impedance).
//   Channel 1: PA4  DAC1_OUT1, DMA1 channel 3, request 6
//              Shares a net with SW2
//              (don't press SW2 while using it).
//   TIM6       sample clock, update event -> TRGO -> DAC trigger (TSEL = 000)
//
// The DAC only updates its output on a TIM6 trigger (TEN = 1). To change the
// output without DMA we write DHR and then force a TIM6 update event (UG),
// which also fires TRGO.

#include "audio.h"
#include "STM32L432KC_GPIO.h"

#define SILENCE 128u

#if AUDIO_DAC_CHANNEL == 2
  #define DAC_PIN        PA5
  #define DAC_DHR8R      (DAC1->DHR8R2)
  #define DAC_EN         DAC_CR_EN2
  #define DAC_TEN        DAC_CR_TEN2
  #define DAC_TSEL       DAC_CR_TSEL2
  #define DAC_WAVE       DAC_CR_WAVE2
  #define DAC_DMAEN      DAC_CR_DMAEN2
  #define DAC_MODE       DAC_MCR_MODE2
  #define DAC_DMAUDR     DAC_SR_DMAUDR2
  #define DMA_CH         DMA1_Channel4
  #define DMA_IRQn       DMA1_Channel4_IRQn
  #define DMA_IRQHandler DMA1_Channel4_IRQHandler
  #define DMA_CSEL_MSK   DMA_CSELR_C4S
  #define DMA_CSEL_POS   DMA_CSELR_C4S_Pos
  #define DMA_REQUEST    5u
  #define DMA_TCIF       DMA_ISR_TCIF4
  #define DMA_CGIF       DMA_IFCR_CGIF4
#elif AUDIO_DAC_CHANNEL == 1
  #define DAC_PIN        PA4
  #define DAC_DHR8R      (DAC1->DHR8R1)
  #define DAC_EN         DAC_CR_EN1
  #define DAC_TEN        DAC_CR_TEN1
  #define DAC_TSEL       DAC_CR_TSEL1
  #define DAC_WAVE       DAC_CR_WAVE1
  #define DAC_DMAEN      DAC_CR_DMAEN1
  #define DAC_MODE       DAC_MCR_MODE1
  #define DAC_DMAUDR     DAC_SR_DMAUDR1
  #define DMA_CH         DMA1_Channel3
  #define DMA_IRQn       DMA1_Channel3_IRQn
  #define DMA_IRQHandler DMA1_Channel3_IRQHandler
  #define DMA_CSEL_MSK   DMA_CSELR_C3S
  #define DMA_CSEL_POS   DMA_CSELR_C3S_Pos
  #define DMA_REQUEST    6u
  #define DMA_TCIF       DMA_ISR_TCIF3
  #define DMA_CGIF       DMA_IFCR_CGIF3
#else
  #error "AUDIO_DAC_CHANNEL must be 1 (PA4) or 2 (PA5)"
#endif

static volatile int playing = 0;

#if !AUDIO_USE_DMA
static const uint8_t *next_sample;   // polled playback: next byte to output
static uint32_t       samples_left;
#endif

// Put `value` on the DAC output right now (TIM6 must be stopped).
static void dacSetNow(uint8_t value) {
    DAC_DHR8R = value;
    TIM6->EGR = TIM_EGR_UG;   // update event -> TRGO -> DAC loads DHR
    TIM6->SR  = 0;
}

void audioInit(void) {
    // Clocks
    RCC->AHB1ENR  |= RCC_AHB1ENR_DMA1EN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_DAC1EN | RCC_APB1ENR1_TIM6EN;

    gpioEnable(GPIO_PORT_A);
    gpioEnable(GPIO_PORT_B);
    pinMode(DAC_PIN, GPIO_ANALOG);
    pinResistor(DAC_PIN, GPIO_FLOATING);
#if AUDIO_DAC_CHANNEL == 2
    // PB7 is tied to PA5 on the Nucleo if SB18 is still there. Make sure
    // PB7 at least isn't driving or pulling.
    pinMode(PB7, GPIO_ANALOG);
    pinResistor(PB7, GPIO_FLOATING);
#endif

    // ---- TIM6: one update event per sample ----
    TIM6->CR1 = 0;                        // stopped
    TIM6->PSC = 0;                        // count at the timer clock (80 MHz)
    TIM6->ARR = (SystemCoreClock / AUDIO_SAMPLE_RATE) - 1;   // 80e6/8000 = 10000
    TIM6->CR2 = (TIM6->CR2 & ~TIM_CR2_MMS) | TIM_CR2_MMS_1;  // MMS = 010: update -> TRGO
    TIM6->EGR = TIM_EGR_UG;               // load PSC/ARR now
    TIM6->SR  = 0;

    // ---- DAC ----
    DAC1->CR  &= ~DAC_EN;                 // config bits only change while disabled
    DAC1->MCR &= ~DAC_MODE;               // normal mode, output buffer on, to pin
    DAC1->CR  &= ~(DAC_TSEL | DAC_WAVE | DAC_DMAEN);  // TSEL = 000: TIM6 TRGO
    DAC1->CR  |= DAC_TEN;
    DAC1->CR  |= DAC_EN;
    for (volatile int i = 0; i < 1000; i++);   // DAC wake-up time (~10 us)
    dacSetNow(SILENCE);                   // idle at mid-scale (~1.65 V)

    // ---- DMA ----
    DMA_CH->CCR = 0;                      // disabled while configuring
    DMA1_CSELR->CSELR = (DMA1_CSELR->CSELR & ~DMA_CSEL_MSK)
                      | (DMA_REQUEST << DMA_CSEL_POS);
    DMA_CH->CPAR = (uint32_t) &DAC_DHR8R;
    // MSIZE = 8 bit, PSIZE = 32 bit (DAC registers are word access),
    // memory increments, memory -> peripheral, interrupt at end.
    DMA_CH->CCR = DMA_CCR_PSIZE_1 | DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_TCIE;

    NVIC_SetPriority(DMA_IRQn, 2);
    NVIC_EnableIRQ(DMA_IRQn);
}

static void stopPlayback(void) {
    TIM6->CR1 &= ~TIM_CR1_CEN;
    DMA_CH->CCR &= ~DMA_CCR_EN;
    DAC1->CR &= ~DAC_DMAEN;
    DMA1->IFCR = DMA_CGIF;
    DAC1->SR = DAC_DMAUDR;                // clear underrun flag if set
}

void audioPlay(const uint8_t *samples, uint32_t length) {
    if (length == 0) return;

    stopPlayback();

#if AUDIO_USE_DMA
    DMA_CH->CMAR  = (uint32_t) samples;
    DMA_CH->CNDTR = length;
    playing = 1;
    DMA_CH->CCR |= DMA_CCR_EN;
    DAC1->CR |= DAC_DMAEN;                // each TIM6 trigger now requests a byte
#else
    DAC_DHR8R    = samples[0];            // output on the first TIM6 tick
    next_sample  = samples + 1;
    samples_left = length - 1;
    TIM6->SR = 0;
    playing = 1;
#endif

    TIM6->CNT = 0;
    TIM6->CR1 |= TIM_CR1_CEN;             // go
}

int audioBusy(void) {
    return playing;
}

void audioService(void) {
#if !AUDIO_USE_DMA
    // Every TIM6 update (125 us) the DAC outputs what's in DHR. Load the next
    // sample right after, so it is ready for the following tick.
    if (playing && (TIM6->SR & TIM_SR_UIF)) {
        TIM6->SR = 0;
        if (samples_left) {
            DAC_DHR8R = *next_sample++;
            samples_left--;
        } else {
            TIM6->CR1 &= ~TIM_CR1_CEN;
            dacSetNow(SILENCE);
            playing = 0;
        }
    }
#endif
}

void audioTestTone(uint32_t ms) {
    // No DMA: TIM6 runs at 1 ms per update. Each update triggers the DAC,
    // which outputs whatever we put in DHR during the previous millisecond.
    stopPlayback();
    playing = 1;

    TIM6->PSC = (SystemCoreClock / 1000000) - 1;   // 1 us ticks
    TIM6->ARR = 999;                               // update every 1 ms
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR  = 0;
    TIM6->CR1 |= TIM_CR1_CEN;

    for (uint32_t i = 0; i < ms; i++) {
        DAC_DHR8R = (i & 1) ? 64 : 192;            // 500 Hz square wave
        while (!(TIM6->SR & TIM_SR_UIF));
        TIM6->SR = 0;
    }

    // Back to the audio sample rate, idling at silence
    TIM6->CR1 &= ~TIM_CR1_CEN;
    TIM6->PSC = 0;
    TIM6->ARR = (SystemCoreClock / AUDIO_SAMPLE_RATE) - 1;
    dacSetNow(SILENCE);                            // also reloads PSC/ARR
    playing = 0;
}

// Runs once when DMA has handed the DAC the last sample.
void DMA_IRQHandler(void) {
    if (DMA1->ISR & DMA_TCIF) {
        stopPlayback();
        dacSetNow(SILENCE);   // park the output at mid-scale (no click/DC step)
        playing = 0;
    }
}
