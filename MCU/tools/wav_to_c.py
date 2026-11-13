#!/usr/bin/env python3
"""
wav_to_c.py - turn an Audacity export into a C array for audio.c.

Audacity export settings (File > Export Audio):
    Channels:    Mono
    Sample Rate: 8000 Hz
    Format:      WAV, Encoding "Unsigned 8-bit PCM"
                 (or "Other uncompressed files" > RAW (header-less) >
                  Unsigned 8-bit PCM, then pass --raw)

Voice recordings work too: silence at the start/end is trimmed and the volume
is normalized automatically (turn off with --no-trim / --no-normalize).

Other WAV formats (16-bit, stereo, 44.1 kHz) also work. They get mixed to
mono, resampled and converted to unsigned 8-bit.

Usage:
    python tools/wav_to_c.py hit.wav  --name hit_sound -o hit_sound.h
    python tools/wav_to_c.py hit.raw  --raw --name hit_sound -o hit_sound.h

Until the real sounds exist, make a stand-in "hit" sound. This writes an
unsigned 8-bit, 8 kHz WAV (the same format Audacity exports) that you can
play on a PC, then converts it like a real export:
    python tools/wav_to_c.py --synth-hit hit_sound.wav --name hit_sound -o hit_sound.h

A stand-in shoot sound:
    python tools/wav_to_c.py --synth-shot sounds/shot_sound.wav --name shot_sound -o shot_sound.h

A longer test melody (Ode to Joy, ~14 s) for checking the speaker/amp:
    python tools/wav_to_c.py --synth-tune sounds/tune.wav --name tune -o tune.h
"""

import argparse
import math
import random
import struct
import sys
import wave

SILENCE = 128
TAIL_SAMPLES = 16   # silence added at the end so the DAC stops at mid-scale


def read_wav(path):
    """Return (samples as floats in -1..1, sample rate)."""
    with wave.open(path, "rb") as w:
        nch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)

    if width == 1:      # unsigned 8-bit
        vals = [(b - 128) / 128.0 for b in raw]
    elif width == 2:    # signed 16-bit little endian
        vals = [v / 32768.0 for v in struct.unpack("<%dh" % (len(raw) // 2), raw)]
    elif width == 3:    # signed 24-bit
        vals = []
        for i in range(0, len(raw), 3):
            v = int.from_bytes(raw[i:i + 3], "little", signed=True)
            vals.append(v / 8388608.0)
    elif width == 4:    # signed 32-bit int
        vals = [v / 2147483648.0 for v in struct.unpack("<%di" % (len(raw) // 4), raw)]
    else:
        sys.exit("Unsupported WAV sample width: %d bytes" % width)

    # Mix to mono
    mono = [sum(vals[i:i + nch]) / nch for i in range(0, len(vals), nch)]
    return mono, rate


def read_raw_u8(path):
    with open(path, "rb") as f:
        return [(b - 128) / 128.0 for b in f.read()]


def resample(x, src_rate, dst_rate):
    """Linear interpolation. Fine for sound effects; use Audacity for best quality.

    When going down in rate (e.g. 48 kHz -> 8 kHz), each output sample is the
    average of the input samples it covers. That removes most content above
    the new Nyquist limit, which would otherwise fold back as harsh noise."""
    if src_rate == dst_rate or not x:
        return x
    n_out = int(len(x) * dst_rate / src_rate)
    out = []
    if dst_rate < src_rate:
        step = src_rate / dst_rate
        for i in range(n_out):
            start = int(i * step)
            end = max(start + 1, min(len(x), int((i + 1) * step)))
            window = x[start:end]
            out.append(sum(window) / len(window))
        return out
    for i in range(n_out):
        pos = i * src_rate / dst_rate
        j = int(pos)
        frac = pos - j
        a = x[j]
        b = x[j + 1] if j + 1 < len(x) else x[j]
        out.append(a + (b - a) * frac)
    return out


def trim_silence(x, threshold=0.10, pad_s=0.02, rate=8000):
    """Cut quiet parts off the start and end (voice recordings usually have
    a second of room noise before you speak). Anything under 10% of the peak
    counts as quiet. Keeps 20 ms around the sound."""
    loud = [i for i, v in enumerate(x) if abs(v) > threshold]
    if not loud:
        return x
    pad = int(pad_s * rate)
    return x[max(0, loud[0] - pad): loud[-1] + pad + 1]


def normalize(x, peak=0.9):
    """Scale so the loudest sample hits `peak`. Microphone recordings are
    often very quiet, which at 8 bits leaves almost nothing to hear."""
    m = max((abs(v) for v in x), default=0.0)
    return [v * peak / m for v in x] if m > 0 else x


def remove_dc(x):
    """Center the waveform on zero (some mics add a constant offset)."""
    if not x:
        return x
    avg = sum(x) / len(x)
    return [v - avg for v in x]


def to_u8(x):
    return [max(0, min(255, int(round(v * 127.0)) + 128)) for v in x]


def synth_hit(rate, seconds=0.40, seed=155):
    """A short 'you got hit' zap: a falling square-ish tone plus a noise
    burst, with a fast decay."""
    rng = random.Random(seed)
    n = int(rate * seconds)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / n                                   # 0..1 through the sound
        freq = 1400.0 * (1 - t) ** 2 + 180.0        # 1580 Hz falling to 180 Hz
        phase += 2 * math.pi * freq / rate
        tone = math.tanh(3.0 * math.sin(phase))     # soft square wave
        noise = rng.uniform(-1, 1) * max(0.0, 1 - 6 * t)   # crunch at the start
        env = (1 - t) ** 1.5                        # decay
        attack = min(1.0, i / (0.004 * rate))       # 4 ms fade-in, avoids a click
        out.append(0.85 * attack * env * (0.75 * tone + 0.35 * noise))
    return out


def synth_shot(rate, seconds=0.20):
    """A 'pew' laser shot: a quick sweep from high to low pitch."""
    n = int(rate * seconds)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / n
        freq = 300.0 + 1900.0 * math.exp(-5.0 * t)   # 2200 Hz falling to ~310 Hz
        phase += 2 * math.pi * freq / rate
        tone = math.tanh(2.0 * math.sin(phase))
        env = (1 - t) ** 2                            # fast fade out
        attack = min(1.0, i / (0.002 * rate))         # 2 ms fade-in, no click
        out.append(0.8 * attack * env * tone)
    return out


# Ode to Joy (Beethoven, public domain). (note, beats)
NOTE_HZ = {"G4": 392.00, "C5": 523.25, "D5": 587.33, "E5": 659.25,
           "F5": 698.46, "G5": 783.99}
ODE_TO_JOY = (
    [("E5", 1), ("E5", 1), ("F5", 1), ("G5", 1), ("G5", 1), ("F5", 1), ("E5", 1), ("D5", 1),
     ("C5", 1), ("C5", 1), ("D5", 1), ("E5", 1), ("E5", 1.5), ("D5", .5), ("D5", 2)] +
    [("E5", 1), ("E5", 1), ("F5", 1), ("G5", 1), ("G5", 1), ("F5", 1), ("E5", 1), ("D5", 1),
     ("C5", 1), ("C5", 1), ("D5", 1), ("E5", 1), ("D5", 1.5), ("C5", .5), ("C5", 2)] +
    [("D5", 1), ("D5", 1), ("E5", 1), ("C5", 1), ("D5", 1), ("E5", .5), ("F5", .5), ("E5", 1), ("C5", 1),
     ("D5", 1), ("E5", .5), ("F5", .5), ("E5", 1), ("D5", 1), ("C5", 1), ("D5", 1), ("G4", 2)] +
    [("E5", 1), ("E5", 1), ("F5", 1), ("G5", 1), ("G5", 1), ("F5", 1), ("E5", 1), ("D5", 1),
     ("C5", 1), ("C5", 1), ("D5", 1), ("E5", 1), ("D5", 1.5), ("C5", .5), ("C5", 2)]
)


def synth_tune(rate, beat_s=0.22, notes=ODE_TO_JOY):
    """A simple melody: soft square-wave notes with a short gap between them."""
    out = []
    for name, beats in notes:
        freq = NOTE_HZ[name]
        n = int(rate * beat_s * beats)
        attack, release = int(0.005 * rate), int(0.03 * rate)
        for i in range(n):
            tone = math.tanh(3.0 * math.sin(2 * math.pi * freq * i / rate))
            env = min(1.0, i / attack, (n - i) / release)
            env *= 1.0 - 0.3 * i / n                 # slight decay through the note
            out.append(0.6 * env * tone)
    return out


def write_u8_wav(path, samples_u8, rate):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(rate)
        w.writeframes(bytes(samples_u8))


def write_header(path, name, samples_u8, rate, source):
    guard = name.upper() + "_H"
    lines = [
        "// %s" % path.replace("\\", "/").split("/")[-1],
        "// Generated by tools/wav_to_c.py from %s. Do not edit by hand." % source,
        "// %d samples, unsigned 8-bit mono, %d Hz (%.3f s). 128 = silence." % (
            len(samples_u8), rate, len(samples_u8) / rate),
        "",
        "#ifndef %s" % guard,
        "#define %s" % guard,
        "",
        "#include <stdint.h>",
        "",
        "#define %s_RATE   %d" % (name.upper(), rate),
        "#define %s_LENGTH %d" % (name.upper(), len(samples_u8)),
        "",
        "static const uint8_t %s[%s_LENGTH] = {" % (name, name.upper()),
    ]
    for i in range(0, len(samples_u8), 16):
        lines.append("    " + ", ".join("%3d" % s for s in samples_u8[i:i + 16]) + ",")
    lines += ["};", "", "#endif", ""]
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("input", nargs="?", help="WAV (or RAW with --raw) exported from Audacity")
    p.add_argument("--raw", action="store_true", help="input is header-less unsigned 8-bit PCM")
    p.add_argument("--raw-rate", type=int, default=8000, help="sample rate of a --raw file (default 8000)")
    p.add_argument("--synth-hit", metavar="WAV_OUT",
                   help="make a stand-in hit sound, save it as an 8-bit WAV, and convert it")
    p.add_argument("--synth-shot", metavar="WAV_OUT",
                   help="make a stand-in shoot ('pew') sound, save it as an 8-bit WAV, and convert it")
    p.add_argument("--synth-tune", metavar="WAV_OUT",
                   help="make a test melody (Ode to Joy), save it as an 8-bit WAV, and convert it")
    p.add_argument("--no-trim", action="store_true",
                   help="keep silence at the start/end (trimmed by default)")
    p.add_argument("--no-normalize", action="store_true",
                   help="keep the original volume (by default the peak is raised to 90%%)")
    p.add_argument("--rate", type=int, default=8000, help="output sample rate (must match AUDIO_SAMPLE_RATE)")
    p.add_argument("--name", default="sound", help="C array name, e.g. hit_sound")
    p.add_argument("-o", "--output", required=True, help="output .h file")
    args = p.parse_args()

    if args.synth_hit:
        u8 = to_u8(synth_hit(args.rate))
        write_u8_wav(args.synth_hit, u8, args.rate)
        print("Wrote %s (8-bit unsigned, %d Hz) - listen to it on your PC" % (args.synth_hit, args.rate))
        args.input = args.synth_hit

    if args.synth_shot:
        u8 = to_u8(synth_shot(args.rate))
        write_u8_wav(args.synth_shot, u8, args.rate)
        print("Wrote %s (8-bit unsigned, %d Hz) - listen to it on your PC" % (args.synth_shot, args.rate))
        args.input = args.synth_shot

    if args.synth_tune:
        u8 = to_u8(synth_tune(args.rate))
        write_u8_wav(args.synth_tune, u8, args.rate)
        print("Wrote %s (8-bit unsigned, %d Hz) - listen to it on your PC" % (args.synth_tune, args.rate))
        args.input = args.synth_tune

    if not args.input:
        p.error("give an input file or --synth-hit")

    if args.raw:
        x, rate = read_raw_u8(args.input), args.raw_rate
    else:
        x, rate = read_wav(args.input)

    if rate != args.rate:
        print("Resampling %d Hz -> %d Hz (better: set Project Rate to %d in Audacity)" % (rate, args.rate, args.rate))
    x = remove_dc(resample(x, rate, args.rate))
    if not args.no_trim:
        x = normalize(x)        # so the silence threshold is relative to the peak
        before = len(x)
        x = trim_silence(x, rate=args.rate)
        if len(x) < before:
            print("Trimmed silence: %.2f s -> %.2f s" % (before / args.rate, len(x) / args.rate))
    if not args.no_normalize:
        x = normalize(x)
    # Short fades so the sound doesn't start or end with a click
    fade = min(len(x) // 4, int(0.005 * args.rate))
    for i in range(fade):
        x[i] *= i / fade
        x[-1 - i] *= i / fade
    u8 = to_u8(x) + [SILENCE] * TAIL_SAMPLES

    write_header(args.output, args.name, u8, args.rate, args.input.replace("\\", "/").split("/")[-1])
    print("Wrote %s: %d samples (%.2f s, %d bytes of flash)" % (args.output, len(u8), len(u8) / args.rate, len(u8)))


if __name__ == "__main__":
    main()
