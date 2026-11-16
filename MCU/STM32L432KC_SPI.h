// STM32L432KC_SPI.h
// Header for SPI1 functions (master mode)
//
// Pins (AF5): PB3 = SCK, PB4 = MISO, PB5 = MOSI.
// Chip select is a plain GPIO handled by the caller.

#ifndef STM32L4_SPI_H
#define STM32L4_SPI_H

#include <stdint.h>
#include <stm32l432xx.h>

///////////////////////////////////////////////////////////////////////////////
// Function prototypes
///////////////////////////////////////////////////////////////////////////////

/* Enables SPI1 as an 8-bit, MSB-first master.
 *    -- br:   baud rate divider, 0..7. SCK = PCLK2 / 2^(br+1)
 *             (at 80 MHz: 0 = 40 MHz ... 5 = 1.25 MHz, 7 = 312.5 kHz)
 *    -- cpol: clock idle level (0 = low)
 *    -- cpha: 0 = sample on the first clock edge, 1 = on the second
 *    Mode 0 = cpol 0, cpha 0. */
void initSPI(int br, int cpol, int cpha);

/* Sends one byte and returns the byte received at the same time. */
uint8_t spiSendReceive(uint8_t send);

#endif
