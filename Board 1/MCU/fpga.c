// fpga.c
// SPI + interrupt link to the FPGA. See fpga.h.

#include "fpga.h"
#include "STM32L432KC_GPIO.h"
#include "STM32L432KC_SPI.h"

#define CS_PIN   PA11
#define HIT_PIN  PA6
#define FIRE_PIN PB0

#define CMD_READ_STATUS 0x00
#define CMD_WRITE_CTRL  0x01

#define SPI_BR_1_25MHZ  5   // 80 MHz / 64

static volatile int hit_edge  = 0;
static volatile int fire_edge = 0;

void fpgaInit(void) {
    gpioEnable(GPIO_PORT_A);
    gpioEnable(GPIO_PORT_B);

    // CS idles high (not selected)
    digitalWrite(CS_PIN, PIO_HIGH);
    pinMode(CS_PIN, GPIO_OUTPUT);

    initSPI(SPI_BR_1_25MHZ, 0, 0);   // mode 0

    // PB6 is tied to PA6 on the Nucleo (SB16). Keep it high-impedance.
    pinMode(PB6, GPIO_ANALOG);
    pinResistor(PB6, GPIO_FLOATING);

    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    // HIT: PA6 -> EXTI line 6, rising edge
    pinMode(HIT_PIN, GPIO_INPUT);
    pinResistor(HIT_PIN, GPIO_PULL_DOWN);   // stays low if SW7 switch 4 is OFF
    SYSCFG->EXTICR[1] &= ~SYSCFG_EXTICR2_EXTI6;   // 000 = port A
    EXTI->RTSR1 |= EXTI_RTSR1_RT6;
    EXTI->FTSR1 &= ~EXTI_FTSR1_FT6;
    EXTI->PR1    = EXTI_PR1_PIF6;                 // clear anything stale
    EXTI->IMR1  |= EXTI_IMR1_IM6;

    // FIRE: PB0 -> EXTI line 0, rising edge
    pinMode(FIRE_PIN, GPIO_INPUT);
    pinResistor(FIRE_PIN, GPIO_PULL_DOWN);  // stays low if the jumper is missing
    SYSCFG->EXTICR[0] = (SYSCFG->EXTICR[0] & ~SYSCFG_EXTICR1_EXTI0)
                      | SYSCFG_EXTICR1_EXTI0_PB;  // 001 = port B
    EXTI->RTSR1 |= EXTI_RTSR1_RT0;
    EXTI->FTSR1 &= ~EXTI_FTSR1_FT0;
    EXTI->PR1    = EXTI_PR1_PIF0;
    EXTI->IMR1  |= EXTI_IMR1_IM0;

    NVIC_SetPriority(EXTI9_5_IRQn, 1);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
    NVIC_SetPriority(EXTI0_IRQn, 1);
    NVIC_EnableIRQ(EXTI0_IRQn);
}

void EXTI9_5_IRQHandler(void) {
    if (EXTI->PR1 & EXTI_PR1_PIF6) {
        EXTI->PR1 = EXTI_PR1_PIF6;   // write 1 to clear
        hit_edge = 1;
    }
}

void EXTI0_IRQHandler(void) {
    if (EXTI->PR1 & EXTI_PR1_PIF0) {
        EXTI->PR1 = EXTI_PR1_PIF0;   // write 1 to clear
        fire_edge = 1;
    }
}

int fpgaHitPending(void) {
    // HIT is a 1 ms pulse per hit; the EXTI catches its rising edge.
    if (hit_edge) {
        hit_edge = 0;
        return 1;
    }
    return 0;
}

int fpgaFirePending(void) {
    if (fire_edge) {
        fire_edge = 0;
        return 1;
    }
    return 0;
}

// Short delay so the FPGA (24 MHz, 3-clock synchronizer) sees CS change
// before the first SCK edge / after the last one. ~1 us at 80 MHz.
static void csDelay(void) {
    for (volatile int i = 0; i < 20; i++);
}

uint8_t fpgaReadStatus(void) {
    digitalWrite(CS_PIN, PIO_LOW);
    csDelay();
    uint8_t status = spiSendReceive(CMD_READ_STATUS);
    csDelay();
    digitalWrite(CS_PIN, PIO_HIGH);
    csDelay();
    return status;
}

void fpgaSetArmed(int armed) {
    digitalWrite(CS_PIN, PIO_LOW);
    csDelay();
    spiSendReceive(CMD_WRITE_CTRL);
    spiSendReceive(armed ? 0x01 : 0x00);
    csDelay();
    digitalWrite(CS_PIN, PIO_HIGH);
    csDelay();
}
