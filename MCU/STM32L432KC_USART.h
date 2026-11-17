// STM32F401RE_USART.h
// Header for USART functions

#ifndef STM32L4_USART_H
#define STM32L4_USART_H

#include <stdint.h>
#include <stm32l432xx.h>

// Defines for USART case statements
#define USART1_ID   1
#define USART2_ID   2

///////////////////////////////////////////////////////////////////////////////
// Function prototypes
///////////////////////////////////////////////////////////////////////////////

USART_TypeDef * id2Port(int USART_ID);
USART_TypeDef * initUSART(int USART_ID, int baud_rate);
void sendChar(USART_TypeDef * USART, char data);
char readChar(USART_TypeDef * USART);
void sendString(USART_TypeDef * USART, char * charArray);
void readString(USART_TypeDef * USART, char * charArray);

// Interrupt-driven receive (USART1 only). Call uartEnableRxInterrupt() once
// after initUSART(USART1_ID, ...).
void uartEnableRxInterrupt(void);

// Copies the next complete line (without its '\n') into `line` and returns 1.
// Returns 0 right away if no complete line has arrived yet.
int uartReadLine(char * line, int max_len);

#endif