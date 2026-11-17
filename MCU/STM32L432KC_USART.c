// STM32L432KC_USART.c
// Source code for USART functions

#include "STM32L432KC.h"
#include "STM32L432KC_USART.h"
#include "STM32L432KC_GPIO.h"
#include "STM32L432KC_RCC.h"

USART_TypeDef * id2Port(int USART_ID) {
    USART_TypeDef * USART;
    switch(USART_ID){
        case(USART1_ID) :
            USART = USART1;
            break;
        case(USART2_ID) :
            USART = USART2;
            break;
        default :
            USART = 0;
    }
    return USART;
}

USART_TypeDef * initUSART(int USART_ID, int baud_rate) {
    gpioEnable(GPIO_PORT_A);  // Enable clock for GPIOA
    RCC->CR |= RCC_CR_HSION;  // Turn on HSI 16 MHz clock
    while (!(RCC->CR & RCC_CR_HSIRDY)); // Wait until HSI is stable

    USART_TypeDef * USART = id2Port(USART_ID); // Get pointer to USART

    switch(USART_ID){
        case USART1_ID :
            RCC->APB2ENR |= RCC_APB2ENR_USART1EN; // Set USART1EN
            RCC->CCIPR &= ~RCC_CCIPR_USART1SEL;
            RCC->CCIPR |= (0b10 << RCC_CCIPR_USART1SEL_Pos); // Set HSI16 (16 MHz) as USART clock source

            // Configure correct alternate functions (AF7 = USART1 on PA9/PA10)
            GPIOA->AFR[1] &= ~(GPIO_AFRH_AFSEL9 | GPIO_AFRH_AFSEL10);
            GPIOA->AFR[1] |= (0b111 << GPIO_AFRH_AFSEL9_Pos) | (0b111 << GPIO_AFRH_AFSEL10_Pos);

            // Configure pin modes as ALT function
            pinMode(PA9, GPIO_ALT); // TX
            pinMode(PA10, GPIO_ALT); // RX

            break;
        case USART2_ID :
            RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN; // Set USART2EN
            RCC->CCIPR &= ~RCC_CCIPR_USART2SEL;
            RCC->CCIPR |= (0b10 << RCC_CCIPR_USART2SEL_Pos); // Set HSI16 (16 MHz) as USART clock source

            // Configure pin modes as ALT function
            pinMode(PA2, GPIO_ALT); // TX
            pinMode(PA15, GPIO_ALT); // RX

            // Configure correct alternate functions
            GPIOA->AFR[0] &= ~GPIO_AFRL_AFSEL2;
            GPIOA->AFR[1] &= ~GPIO_AFRH_AFSEL15;
            GPIOA->AFR[0] |= (0b111 << GPIO_AFRL_AFSEL2_Pos);   //AF7
            GPIOA->AFR[1] |= (0b011 << GPIO_AFRH_AFSEL15_Pos);  //AF3
            break;
    }

    // USART must be disabled (UE = 0) while M, OVER8, STOP and BRR are written
    USART->CR1 &= ~USART_CR1_UE;

    USART->CR1 &= ~(USART_CR1_M1 | USART_CR1_M0); // M=00 corresponds to 1 start bit, 8 data bits, n stop bits
    USART->CR1 &= ~USART_CR1_OVER8;               // OVER8 = 0: 16 times oversampling
    USART->CR2 &= ~USART_CR2_STOP;                // 0b00 corresponds to 1 stop bit
    USART->CR1 &= ~USART_CR1_PCE;                 // No parity

    // Tx/Rx baud = f_CK/USARTDIV (since oversampling by 16)
    // f_CK = 16 MHz (HSI)
    // e.g. 125000 baud -> USARTDIV = 128 (exact); 9600 baud -> 1667 (0.02% error)
    USART->BRR = (HSI_FREQ + baud_rate/2) / baud_rate; // Round to nearest

    USART->CR1 |= USART_CR1_UE;                   // Enable USART
    USART->CR1 |= (USART_CR1_TE | USART_CR1_RE);  // Enable transmission and reception

    return USART;
}

void sendChar(USART_TypeDef * USART, char data){
  while (!(USART->ISR & USART_ISR_TXE)); // Wait until transmit data register is empty
  USART->TDR = data;                     // Writing TDR starts the transfer and clears TXE
  while (!(USART->ISR & USART_ISR_TC));  // Wait until the frame has fully shifted out
}

void sendString(USART_TypeDef * USART, char * charArray){

    uint32_t i = 0;
    do{
        sendChar(USART, charArray[i]);
        i++;
    }
    while(charArray[i] != 0);
}

char readChar(USART_TypeDef * USART) {
        char data = USART->RDR;
        return data;
}

void readString(USART_TypeDef * USART, char* charArray){
    int i = 0;
    do{
        charArray[i] = readChar(USART);
        i++;
    }
    while(USART->ISR & USART_ISR_RXNE);
}

///////////////////////////////////////////////////////////////////////////////
// Interrupt-driven receive for USART1
// Every received byte goes into a ring buffer, so nothing is lost while the
// main loop is busy. uartReadLine() hands the bytes back one line at a time.
///////////////////////////////////////////////////////////////////////////////

#define RX_BUF_SIZE  128   // ring buffer, bytes
#define RX_LINE_SIZE 64    // longest line kept

// Overrun, noise and framing errors. Their clear bits in ICR sit at the same
// bit positions as the flags in ISR.
#define RX_ERRORS (USART_ISR_ORE | USART_ISR_NE | USART_ISR_FE)

static volatile char     rx_buf[RX_BUF_SIZE];
static volatile uint32_t rx_head = 0;   // next free slot (written by the ISR)
static volatile uint32_t rx_tail = 0;   // next byte to read (main loop)

static char rx_line[RX_LINE_SIZE];
static int  rx_line_len = 0;

void uartEnableRxInterrupt(void) {
    USART1->CR1 |= USART_CR1_RXNEIE;
    NVIC_SetPriority(USART1_IRQn, 2);
    NVIC_EnableIRQ(USART1_IRQn);
}

void USART1_IRQHandler(void) {
    uint32_t isr = USART1->ISR;

    // An uncleared overrun would re-enter this handler forever.
    if (isr & RX_ERRORS) USART1->ICR = RX_ERRORS;

    if (isr & USART_ISR_RXNE) {
        char c = (char)USART1->RDR;                 // reading RDR clears RXNE
        uint32_t next = (rx_head + 1) % RX_BUF_SIZE;
        if (next != rx_tail) {                      // drop the byte if full
            rx_buf[rx_head] = c;
            rx_head = next;
        }
    }
}

int uartReadLine(char *line, int max_len) {
    while (rx_tail != rx_head) {
        char c = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) % RX_BUF_SIZE;

        if (c == '\r') continue;

        if (c == '\n') {
            int n = rx_line_len < max_len - 1 ? rx_line_len : max_len - 1;
            for (int i = 0; i < n; i++) line[i] = rx_line[i];
            line[n] = 0;
            rx_line_len = 0;
            return 1;
        }

        // Too long to be a request (for example boot noise): start over.
        if (rx_line_len >= RX_LINE_SIZE - 1) rx_line_len = 0;
        rx_line[rx_line_len++] = c;
    }
    return 0;
}
