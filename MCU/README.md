# MCU

MCU firmware, Wi-Fi communication, and scoreboard code.

## Sounds (DAC + DMA)

`main.c` plays `shot_sound` when the FPGA fires a shot and `hit_sound` when it
reports a hit.

| Signal       | MCU pin | Dev board                                   |
|--------------|---------|---------------------------------------------|
| hit_irq in   | PA6     | From FPGA P27 through SW7 switch 4 (**ON**). D4 lights on a hit |
| fire_irq in  | PB4     | From FPGA P12 through SW7 switch 7 (**ON**) |
| Audio out    | PA5     | DAC1_OUT2. SW7 switch 3 must stay **OFF** |
| Test button  | PA7     | SW3. Plays the hit sound without the FPGA   |

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

Right now `hit_sound.h` comes from a synthesized stand-in,
`sounds/hit_sound.wav`, which is in the same format Audacity exports:
```
python tools/wav_to_c.py --synth-hit  sounds/hit_sound.wav  --name hit_sound  -o hit_sound.h
python tools/wav_to_c.py --synth-shot sounds/shot_sound.wav --name shot_sound -o shot_sound.h
```
