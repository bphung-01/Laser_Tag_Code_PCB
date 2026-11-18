// web.c
// Answers the scoreboard page's requests. See web.h.

#include <string.h>
#include "web.h"
#include "game.h"
#include "webpage.h"

static USART_TypeDef *uart;

void webInit(USART_TypeDef *usart) {
    uart = usart;
}

// Appends `value` in decimal at `p`; returns the new end of the string.
static char *appendNumber(char *p, uint32_t value) {
    char digits[10];
    int  n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n) *p++ = digits[--n];
    return p;
}

static void sendStatus(uint32_t now) {
    char  reply[80];
    char *p = reply;

    strcpy(p, "<!DOCTYPE html><html>LT,");
    p += strlen(p);
    p = appendNumber(p, (uint32_t)gamePhase());   *p++ = ',';
    p = appendNumber(p, (uint32_t)gameLives());   *p++ = ',';
    p = appendNumber(p, (uint32_t)gameHits());    *p++ = ',';
    p = appendNumber(p, (uint32_t)gamePlayer());  *p++ = ',';
    p = appendNumber(p, gameElapsedMs(now));
    strcpy(p, "</html>");

    sendString(uart, reply);
}

void webService(uint32_t now) {
    char line[64];
    if (!uartReadLine(line, sizeof(line))) return;

    // Skip anything that isn't a request (for example ESP8266 boot noise).
    char *request = strstr(line, "/REQ:");
    if (!request) return;
    char *path = request + 5;

    if (strcmp(path, "state") == 0) {
        sendStatus(now);
    } else if (strcmp(path, "start") == 0) {
        gameStart(now);
        sendStatus(now);
    } else if (strcmp(path, "reset") == 0) {
        gameReset();
        sendStatus(now);
    } else {
        sendString(uart, (char *)webpage);
    }
}
