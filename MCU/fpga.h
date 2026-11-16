// fpga.h
// Talks to the FPGA: two interrupt lines for game events, plus SPI1 that is
// set up but not used by main.c yet (kept for LCD data / game control later).
//
//   PA6  HIT   <- FPGA P27  (SW7 switch 4), 1 ms pulse per hit
//   PB0  FIRE  <- FPGA P18  (jumper wire), rising edge = this unit fired
//   PB3  SCK   -> FPGA P21  (SW7 switch 8)
//   PB4  MISO  <- FPGA P12  (SW7 switch 7)
//   PB5  MOSI  -> FPGA P10  (SW7 switch 6)
//   PA11 CS    -> FPGA P20  (SW7 switch 5), active low, held high when idle
//
// The FPGA decides what counts as a hit (only the other player's shots, or
// every packet when SW6 switch 1 is ON for single-board testing).
//
// SPI mode 0, 1.25 MHz. Protocol (see FPGA2/mcu_spi.sv):
//   read status:   send 0x00, receive the status byte
//   write control: send 0x01, then the control byte (bit 0 = armed)

#ifndef FPGA_H
#define FPGA_H

#include <stdint.h>

// Status byte bits (SPI, for later use)
#define FPGA_STATUS_ARMED  (1u << 5)  // firing is enabled
#define FPGA_STATUS_ME_P2  (1u << 4)  // this unit is player 2 (SW6)
#define FPGA_STATUS_ID     0x0Fu      // shooter ID of the last valid packet

#define PLAYER1_ID 0xA   // 1010
#define PLAYER2_ID 0xC   // 1100

// Set up the HIT (EXTI6) and FIRE (EXTI0) interrupt pins, SPI1 and the CS pin.
// Call after configureClock().
void fpgaInit(void);

// 1 once per hit (HIT rising edge). Reading it clears it.
int fpgaHitPending(void);

// 1 once per shot fired (FIRE rising edge). Reading it clears it.
int fpgaFirePending(void);

// SPI (not used by main.c yet)
uint8_t fpgaReadStatus(void);
void    fpgaSetArmed(int armed);

#endif
