// STM32L432KC_SPI.c
// Source code for SPI1 functions (master mode)

#include "STM32L432KC_SPI.h"
#include "STM32L432KC_GPIO.h"

void initSPI(int br, int cpol, int cpha) {
    // Clocks
    gpioEnable(GPIO_PORT_B);
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    // PB3 = SCK, PB4 = MISO, PB5 = MOSI, all alternate function 5
    pinMode(PB3, GPIO_ALT);
    pinMode(PB4, GPIO_ALT);
    pinMode(PB5, GPIO_ALT);
    GPIOB->AFR[0] &= ~(GPIO_AFRL_AFSEL3 | GPIO_AFRL_AFSEL4 | GPIO_AFRL_AFSEL5);
    GPIOB->AFR[0] |= (5 << GPIO_AFRL_AFSEL3_Pos)
                   | (5 << GPIO_AFRL_AFSEL4_Pos)
                   | (5 << GPIO_AFRL_AFSEL5_Pos);
    // Keep the default (low) output speed. 1.25 MHz doesn't need fast edges,
    // and slow edges ring less on the board traces.
    GPIOB->OSPEEDR &= ~(GPIO_OSPEEDR_OSPEED3 | GPIO_OSPEEDR_OSPEED5);

    SPI1->CR1 = 0;   // disabled while configuring
    SPI1->CR1 = (br << SPI_CR1_BR_Pos)
              | (cpol ? SPI_CR1_CPOL : 0)
              | (cpha ? SPI_CR1_CPHA : 0)
              | SPI_CR1_MSTR
              | SPI_CR1_SSM | SPI_CR1_SSI;   // software chip select (we drive CS)

    // 8-bit frames (DS = 0111). FRXTH = 1 so RXNE sets after 8 bits, not 16.
    SPI1->CR2 = (7 << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;

    SPI1->CR1 |= SPI_CR1_SPE;   // enable
}

uint8_t spiSendReceive(uint8_t send) {
    // Use 8-bit accesses to DR. A 32/16-bit write would queue two bytes.
    while (!(SPI1->SR & SPI_SR_TXE));
    *(volatile uint8_t *) &SPI1->DR = send;

    while (!(SPI1->SR & SPI_SR_RXNE));
    return *(volatile uint8_t *) &SPI1->DR;
}
