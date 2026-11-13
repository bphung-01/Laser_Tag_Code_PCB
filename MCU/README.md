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
- USART1 runs at 115200 baud. Until the ESP8266 is connected, it prints events
  (SHOT, HIT) that you can watch with a 3.3 V
  USB-serial adapter on PA9.

## FPGA → MCU events (`fpga.c`)

Two interrupt lines, one per event. No SPI is involved.
- **FIRE (P18 → PB0, jumper wire):** a rising edge means this unit just fired,
  so the MCU plays the pew.
- **HIT (P27 → PA6, SW7 switch 4):** a 1 ms pulse per hit, so the MCU plays the
  hit sound.

The FPGA decides what counts as a hit: only the other player's shots. Turn
**SW6 switch 1 (P32)** ON for test mode, where your own shots count too, so one
board can test hits.

The SPI link (`STM32L432KC_SPI.c`, `FPGA2/mcu_spi.sv`) is wired and set up, but
`main.c` doesn't use it yet. It's kept for LCD data and game control later. SPI
mode 0 at 1.25 MHz: `0x00` reads the status byte, `0x01 <ctrl>` writes the
control register (bit 0 = armed, which is 1 after reset).

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
