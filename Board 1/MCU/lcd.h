// lcd.h
// 16x2 character LCD (HD44780) on a PCF8574 I2C backpack ("LCD1602 I2C").
//
// I2C is bit-banged on two free GPIOs, because every hardware I2C pin pair
// on the STM32L432KC is already used on this board:
//   PA9/PA10 = USART1 (ESP8266), PB6/PB7 = tied to PA6/PA5 by Nucleo solder
//   bridges, PA7/PB4 = SW3 button / SPI MISO.
//
// Wiring (LCD backpack -> Nucleo):
//   GND -> GND
//   VCC -> 5V            (the LCD needs 5 V; 3.3 V gives a blank/faint screen)
//   SDA -> PA12 (D2)
//   SCL -> PA8  (D9)
// The backpack has its own pull-ups to 5 V. PA8 and PA12 are 5 V tolerant and
// the driver only ever pulls them low (open drain), so no level shifter needed.
//
// If the backlight is on but you only see a row of solid blocks (or nothing),
// turn the blue contrast pot on the back of the backpack.

#ifndef LCD_H
#define LCD_H

#include <stdint.h>

#define LCD_SCL_PIN  PA8
#define LCD_SDA_PIN  PA12

#define LCD_COLS 16
#define LCD_ROWS 2

// Finds the backpack (0x27 for PCF8574T, 0x3F for PCF8574AT, or anything in
// 0x20-0x27 / 0x38-0x3F) and initializes the display.
// Returns the 7-bit I2C address, or 0 if nothing answered.
uint8_t lcdInit(void);

void lcdClear(void);
void lcdSetCursor(uint8_t col, uint8_t row);
void lcdPutc(char c);
void lcdPrint(const char *s);
void lcdBacklight(int on);

// Defines custom character slot 0-7 from 8 rows of 5 bits. Print it with
// lcdPutc(slot).
void lcdCreateChar(uint8_t slot, const uint8_t rows[8]);

// Busy-wait delays (DWT cycle counter), usable before interrupts are on.
void lcdDelayUs(uint32_t us);
void lcdDelayMs(uint32_t ms);

#endif
