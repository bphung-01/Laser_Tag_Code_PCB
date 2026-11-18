// web.h
// Answers the scoreboard page's requests, which arrive through the ESP8266
// running the E155 course web server firmware.
//
// How that firmware works (125000 baud on USART1):
//   - For every browser request it sends one line: "/REQ:<path>\n".
//     http://192.168.4.1/start arrives as "/REQ:start", the home page as "/REQ:".
//   - It then forwards whatever the MCU sends back, until it sees "</html>"
//     or 200 ms pass with no bytes. A reply under 10 bytes counts as no reply.
//   - It never speaks first, so the page asks for the status twice a second.
//
// Requests:
//   /state   reply with the status line
//   /start   begin a game, reply with the status line
//   /reset   back to idle, reply with the status line
//   others   reply with the scoreboard page (webpage.h)
//
// Status line:
//   <!DOCTYPE html><html>LT,<phase>,<lives>,<hits>,<player>,<ms></html>
//   phase: 0 idle, 1 countdown, 2 live, 3 over (game.h)
//   player: 1 or 2, which player this board is
//   ms: time since the game went live

#ifndef WEB_H
#define WEB_H

#include <stdint.h>
#include "STM32L432KC_USART.h"

// `usart` must be USART1, already set up with initUSART() and
// uartEnableRxInterrupt().
void webInit(USART_TypeDef *usart);

// Call every main-loop pass. `now` is the 1 ms tick count.
// Sending the full page blocks for about a second; status replies take ~4 ms.
void webService(uint32_t now);

#endif
