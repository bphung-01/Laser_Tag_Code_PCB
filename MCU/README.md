# MCU

MCU firmware, Wi-Fi communication, and scoreboard code.

## Pin plan (E155 dev board)

SW7 connects MCU pins to FPGA pins:

| SW7 | MCU | FPGA | Use | Switch |
|---|---|---|---|---|
| 1 | PA10 | P23 | USART1 RX (ESP8266) | ON or OFF (FPGA ignores P23) |
| 2 | PA9 | P25 | USART1 TX (ESP8266) | ON or OFF (FPGA ignores P25) |
| 3 | PA5 | P26 | DAC audio out | **OFF** |
| 4 | PA6 | P27 | HIT interrupt, FPGA → MCU (lights D4) | **ON** |
| 5 | PA11 | P20 | SPI CS | **ON** |
| 6 | PB5 | P10 | SPI MOSI | **ON** |
| 7 | PB4 | P12 | SPI MISO | **ON** |
| 8 | PB3 | P21 | SPI SCK (lights LD3/D5) | **ON** |

Other pins:
- **Jumper wire FPGA P18 → MCU PB0**: FIRE interrupt (one rising edge per shot).
- FPGA **P13**: IR LED driver (TN2106 gate). It moved from P20, which is now CS.
- FPGA P19: TSOP4838 output.
- MCU PA7: SW3, plays the hit sound (speaker test).
- MCU PB6 and PB7: leave unconnected. Nucleo solder bridges tie them to PA6 and PA5.
- USART1 (PA9 TX, PA10 RX) runs at 125000 baud to the ESP8266. TX and RX cross
  over: MCU TX goes to ESP8266 RX.

## Scoreboard page (`web.c`, `game.c`)

The ESP8266 keeps the E155 course web server firmware. The MCU sends it the
scoreboard page and answers the page's requests.

1. Power the board, join the `Lab6ESP_xx` Wi-Fi network on a laptop.
2. Open `http://192.168.4.1/` (http, not https). The page takes a second or two
   to load.
3. Enter the player names and press **Start game**. After a 3 second countdown
   the game is live: the blaster is armed and each hit costs one of 3 lives.
4. **Reset** puts the board back to idle with full lives.

The blaster is disarmed and hits are ignored unless a game is live, so with
this firmware there is no shot or hit sound until a game is started from the
page. (SW3 still plays the hit sound as a speaker test.)

How it talks to the ESP8266:
- For each browser request the ESP8266 sends one line, `/REQ:<path>`.
  `/start` arrives as `/REQ:start`, the home page as `/REQ:`.
- The MCU replies and the ESP8266 forwards the reply once it sees `</html>`.
- `/state`, `/start` and `/reset` get a short status reply,
  `<!DOCTYPE html><html>LT,<phase>,<lives>,<hits>,<player>,<ms></html>`
  (phase: 0 idle, 1 countdown, 2 live, 3 over). Anything else gets the page.
- The ESP8266 never speaks first, so the page asks for `/state` twice a second.
  A hit shows on the page within about half a second.

This is a one-board demo: the page shows both players, but only this board's
player loses lives. Which player the board is comes from SW6 through the FPGA.

### Editing the page

The page is `../Web/scoreboard.html`. The MCU sends the copy stored in
`webpage.h`, so regenerate that after every edit:
```
python tools/make_webpage.py
```
It strips comments and spacing, and minifies the script if
[bun](https://bun.sh) is installed.

To try the page without the board:
```
python tools/mock_board.py
```
then open `http://127.0.0.1:8155/`. Press Enter in the terminal to fake a hit.
Opening `scoreboard.html` directly as a file runs it as a stand-alone demo.

## FPGA → MCU events (`fpga.c`)

Two interrupt lines, one per event.
- **FIRE (P18 → PB0, jumper wire):** a rising edge means this unit just fired,
  so the MCU plays the pew.
- **HIT (P27 → PA6, SW7 switch 4):** a 1 ms pulse per hit, so the MCU plays the
  hit sound.

The FPGA decides what counts as a hit: only the other player's shots. Turn
**SW6 switch 1 (P32)** ON for test mode, where your own shots count too, so one
board can test hits.

The SPI link (`STM32L432KC_SPI.c`, `FPGA2/mcu_spi.sv`) is used by `game.c` to
arm and disarm the blaster and to read which player this board is. SPI mode 0
at 1.25 MHz: `0x00` reads the status byte, `0x01 <ctrl>` writes the control
register (bit 0 = armed, which is 1 after an FPGA reset).

## Sounds (DAC + DMA)

`main.c` plays `shot_sound` when this unit fires and `hit_sound` when it's hit.

How it works: TIM6 overflows at 8 kHz and triggers DAC channel 2. Each trigger,
DMA1 channel 4 moves the next byte into `DAC1->DHR8R2`. The CPU only starts the
sound, and gets one interrupt when it ends (`audio.c`).

PA5 idles at about 1.65 V and swings 0 to 3.3 V. Put a capacitor (about 10 µF) in
series and a volume pot or divider in front of the LM386 input. The LM386 only
needs a few hundred mV.

### Making sounds in Audacity

1. Set the project rate (bottom left) to **8000 Hz** and make the track mono.
2. File > Export Audio > WAV, Encoding: **Unsigned 8-bit PCM**.
3. Convert it:
   ```
   python tools/wav_to_c.py sounds/hit.wav --name hit_sound -o hit_sound.h
   ```

`shot_sound.h` comes from a voice recording. Silence is trimmed, the volume is
normalized, and the 48 kHz file is resampled to 8 kHz automatically:
```
python tools/wav_to_c.py "../Wave Files/Pew_Joaquin.wav" --name shot_sound -o shot_sound.h
```

Right now `hit_sound.h` comes from a synthesized stand-in,
`sounds/hit_sound.wav`, which is in the same format Audacity exports:
```
python tools/wav_to_c.py --synth-hit  sounds/hit_sound.wav  --name hit_sound  -o hit_sound.h
```
(The old synthesized shot sound is still in `sounds/shot_sound.wav` if needed.)
