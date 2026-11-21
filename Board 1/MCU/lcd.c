// lcd.c
// HD44780 16x2 LCD through a PCF8574 I2C backpack, 4-bit mode.
// See lcd.h for wiring.
//
// PCF8574 output bits -> LCD pins (standard backpack):
//   P0 = RS, P1 = RW, P2 = EN, P3 = backlight, P4..P7 = D4..D7
// RW is always 0 (write only), so the driver waits fixed times instead of
// reading the busy flag.

#include "STM32L432KC.h"
#include "lcd.h"

#define PCF_RS  0x01
#define PCF_RW  0x02
#define PCF_EN  0x04
#define PCF_BL  0x08

// HD44780 commands
#define LCD_CLEAR        0x01
#define LCD_HOME         0x02
#define LCD_ENTRY_MODE   0x06   // increment cursor, no display shift
#define LCD_DISPLAY_ON   0x0C   // display on, cursor off, blink off
#define LCD_FUNC_4BIT_2L 0x28   // 4-bit bus, 2 lines, 5x8 font
#define LCD_SET_CGRAM    0x40
#define LCD_SET_DDRAM    0x80

static uint8_t lcd_addr = 0;
static uint8_t lcd_bl   = PCF_BL;

///////////////////////////////////////////////////////////////////////////////
// Delays (DWT cycle counter)
///////////////////////////////////////////////////////////////////////////////

static void dwtInit(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

void lcdDelayUs(uint32_t us) {
    uint32_t start  = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < cycles) {}
}

void lcdDelayMs(uint32_t ms) {
    while (ms--) lcdDelayUs(1000);
}

///////////////////////////////////////////////////////////////////////////////
// Bit-banged I2C, about 100 kHz. Pins are open drain: writing 1 releases the
// line (the backpack pull-up takes it high), writing 0 pulls it low.
///////////////////////////////////////////////////////////////////////////////

#define I2C_HALF_US 5

static GPIO_TypeDef *scl_port, *sda_port;
static uint32_t scl_bit, sda_bit;

static inline void sclHigh(void) { scl_port->BSRR = scl_bit; }
static inline void sclLow(void)  { scl_port->BRR  = scl_bit; }
static inline void sdaHigh(void) { sda_port->BSRR = sda_bit; }
static inline void sdaLow(void)  { sda_port->BRR  = sda_bit; }
static inline int  sdaRead(void) { return (sda_port->IDR & sda_bit) != 0; }

static void openDrainPin(int pin) {
    GPIO_TypeDef *port = gpioPinToBase(pin);
    int off = gpioPinOffset(pin);
    port->BSRR   = 1u << off;           // released before it becomes an output
    port->OTYPER |= 1u << off;          // open drain
    port->OSPEEDR |= 0b10u << (2*off);  // high speed edges
    pinResistor(pin, GPIO_PULL_UP);     // weak backup; backpack has 4.7k-10k
    pinMode(pin, GPIO_OUTPUT);
}

static void i2cPinsInit(void) {
    gpioEnable(gpioPinToPort(LCD_SCL_PIN));
    gpioEnable(gpioPinToPort(LCD_SDA_PIN));
    scl_port = gpioPinToBase(LCD_SCL_PIN);
    sda_port = gpioPinToBase(LCD_SDA_PIN);
    scl_bit  = 1u << gpioPinOffset(LCD_SCL_PIN);
    sda_bit  = 1u << gpioPinOffset(LCD_SDA_PIN);
    openDrainPin(LCD_SCL_PIN);
    openDrainPin(LCD_SDA_PIN);

    // Bus recovery: if a slave was left mid-byte holding SDA low, clock it out.
    for (int i = 0; i < 9 && !sdaRead(); i++) {
        sclLow();  lcdDelayUs(I2C_HALF_US);
        sclHigh(); lcdDelayUs(I2C_HALF_US);
    }
}

static void i2cStart(void) {
    sdaHigh(); sclHigh(); lcdDelayUs(I2C_HALF_US);
    sdaLow();             lcdDelayUs(I2C_HALF_US);
    sclLow();             lcdDelayUs(I2C_HALF_US);
}

static void i2cStop(void) {
    sdaLow();  lcdDelayUs(I2C_HALF_US);
    sclHigh(); lcdDelayUs(I2C_HALF_US);
    sdaHigh(); lcdDelayUs(I2C_HALF_US);
}

// Sends one byte MSB first. Returns 1 if the slave ACKed.
static int i2cWriteByte(uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        if (b & (1u << i)) sdaHigh(); else sdaLow();
        lcdDelayUs(I2C_HALF_US);
        sclHigh(); lcdDelayUs(I2C_HALF_US);
        sclLow();
    }
    sdaHigh();                       // release SDA for the ACK bit
    lcdDelayUs(I2C_HALF_US);
    sclHigh(); lcdDelayUs(I2C_HALF_US);
    int ack = !sdaRead();
    sclLow();  lcdDelayUs(I2C_HALF_US);
    return ack;
}

// Writes one byte to the PCF8574. Returns 1 on ACK.
static int pcfWrite(uint8_t addr, uint8_t data) {
    i2cStart();
    int ok = i2cWriteByte((uint8_t)(addr << 1));   // R/W = 0
    if (ok) ok = i2cWriteByte(data);
    i2cStop();
    return ok;
}

///////////////////////////////////////////////////////////////////////////////
// LCD, 4-bit transfers through the PCF8574
///////////////////////////////////////////////////////////////////////////////

// Puts the high nibble of `nib` on D4-D7 and pulses EN.
static void lcdWriteNibble(uint8_t nib, uint8_t rs) {
    uint8_t out = (nib & 0xF0) | lcd_bl | rs;
    pcfWrite(lcd_addr, out | PCF_EN);
    lcdDelayUs(1);                   // EN pulse width >= 450 ns
    pcfWrite(lcd_addr, out);
    lcdDelayUs(50);                  // most commands take 37 us
}

static void lcdSend(uint8_t value, uint8_t rs) {
    lcdWriteNibble(value & 0xF0, rs);
    lcdWriteNibble((uint8_t)(value << 4), rs);
}

static void lcdCommand(uint8_t cmd) { lcdSend(cmd, 0); }

uint8_t lcdInit(void) {
    dwtInit();
    i2cPinsInit();

    // Find the backpack: PCF8574T is 0x20-0x27, PCF8574AT is 0x38-0x3F.
    static const uint8_t candidates[] = {
        0x27, 0x3F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E
    };
    lcd_addr = 0;
    for (unsigned i = 0; i < sizeof candidates; i++) {
        if (pcfWrite(candidates[i], 0x00)) { lcd_addr = candidates[i]; break; }
    }
    if (!lcd_addr) return 0;

    lcdDelayMs(50);                  // LCD needs > 40 ms after power-up

    // HD44780 "initialization by instruction" into 4-bit mode
    pcfWrite(lcd_addr, lcd_bl);
    lcdWriteNibble(0x30, 0); lcdDelayMs(5);
    lcdWriteNibble(0x30, 0); lcdDelayUs(150);
    lcdWriteNibble(0x30, 0); lcdDelayUs(150);
    lcdWriteNibble(0x20, 0); lcdDelayUs(150);   // now in 4-bit mode

    lcdCommand(LCD_FUNC_4BIT_2L);
    lcdCommand(0x08);                // display off
    lcdClear();
    lcdCommand(LCD_ENTRY_MODE);
    lcdCommand(LCD_DISPLAY_ON);
    return lcd_addr;
}

void lcdClear(void) {
    lcdCommand(LCD_CLEAR);
    lcdDelayMs(2);                   // clear takes 1.52 ms
}

void lcdSetCursor(uint8_t col, uint8_t row) {
    static const uint8_t row_start[] = { 0x00, 0x40, 0x14, 0x54 };
    if (row >= LCD_ROWS) row = LCD_ROWS - 1;
    if (col >= LCD_COLS) col = LCD_COLS - 1;
    lcdCommand(LCD_SET_DDRAM | (row_start[row] + col));
}

void lcdPutc(char c) { lcdSend((uint8_t)c, PCF_RS); }

void lcdPrint(const char *s) {
    while (*s) lcdPutc(*s++);
}

void lcdBacklight(int on) {
    lcd_bl = on ? PCF_BL : 0;
    pcfWrite(lcd_addr, lcd_bl);
}

void lcdCreateChar(uint8_t slot, const uint8_t rows[8]) {
    lcdCommand(LCD_SET_CGRAM | ((slot & 7) << 3));
    for (int i = 0; i < 8; i++) lcdSend(rows[i] & 0x1F, PCF_RS);
    lcdCommand(LCD_SET_DDRAM);       // back to display RAM
}
