# libmd12

libmd12 is a small C library that reproduces the audio path of the E-mu
SP-1200, and of the SP-12 before it: the sampling input, the 12-bit
converters, the playback controller's pitch counter, what the operating
system does to each note, the output filters, the jacks and the mix. It's
built from E-mu's service manual schematics, E-mu's own firmware, and the one
published set of measurements of a real machine, and every number in it says
where it came from.

It's an engine, not a plugin. Put it in a plugin, a hardware effect or a
command-line tool. It's plain C99 with no allocation, no locks, no operating
system calls and no maths library while audio runs. It works at any sample
rate and block size, gives the same output whatever the block size, and runs
on a bare Cortex-M7 as well as on a desktop.

libmd12 isn't made or endorsed by E-mu Systems or Rossum Electro-Music. The
names SP-1200 and SP-12 are used only to say which machines it reproduces.

## What it reproduces

- **Sampling.** The input preamp and seventh-order anti-aliasing filter, the
  sample and hold, and a truncating 12-bit converter with its trimmed offset,
  at 26,041.67 Hz: the 20 MHz crystal divided by 768.
- **Playback.** E-mu's semitone table and the playback controller's counter:
  a 7-bit fraction that drops or repeats samples with no interpolation, plus
  the end and loop logic. The SP-1200 and the SP-12 turn a pitch byte into a
  step differently, and each preset uses its own machine's rule.
- **Converters.** The 12-bit DAC, then the 8-bit level DAC driven by the
  level PROM, so a quiet sound keeps all twelve bits.
- **The staircase.** Each channel's held output changes at its true moment,
  including its 4.8 microsecond slot in the frame. The steps are band-limited
  to the host's rate, so the images above 13 kHz come through as they do from
  the real outputs, up to the host's Nyquist frequency, without aliasing.
- **Outputs.** Outputs 1 and 2 go through the SSM2044 four-pole filter, which
  every note opens and which closes again over about a quarter of a second.
  Outputs 3 to 6 each have their own fixed filter. Outputs 7 and 8 are
  unfiltered. On the SP-1200, the tip of jacks 1 to 6 is unfiltered and the
  ring filtered. The mix is the inverted sum of the filtered channels that
  have nothing plugged into their jack.
- **The operating system.** The decay table and the 1.2288 ms housekeeping
  tick that steps it, the length of the filter trigger (8.8 to 10.1 ms), the
  delays between a note and its sound starting, and how MIDI velocity sets
  the level.
- **Effect mode.** Live audio through any one channel, as if sampled and
  played back as it arrives. A tuning other than unity is the 45 rpm trick:
  the input is sampled as if sped up by exactly the ratio the tuning slows it
  down by, so pitch and timing are unchanged and only the artefacts change.

## What it leaves out

The sequencer, swing, and loading disk images or MIDI sample dumps aren't
part of the audio path.

Noise, hum, droop and crosstalk in the sample and holds, DAC nonlinearity,
clipping in the mix amplifier, and the SSM2044 at high levels are left out on
purpose. Nothing published describes them well enough to model, and the rule
here is that nothing is invented. The small logic chips that time the sample
and holds would settle much of it, but nobody has published their contents.

## Building

You need a C99 compiler and make.

```
make
```

That builds `build/libmd12.a` and the command-line tools. `make check` runs
the tests. To use the library in your own build, compile `src/*.c` with
`include` and `src` on the include path and link with `-lm`. The maths library
is used only by the init and set functions, never while audio runs.

The table generators and checks also need Python 3 with NumPy and SciPy,
ngspice, an arm-none-eabi toolchain and QEMU. `nix develop` gives you all of
them.

## Using it

Everything is in `include/md12.h`. You own every structure and can put it
anywhere: on the stack, in a static, or in a DSP's fast memory.

### As an effect

```c
#include "md12.h"

static md12_effect fx;

void setup(void)
{
    md12_calib cal;

    md12_calib_sp1200(&cal);
    md12_effect_init(&fx, &cal, 48000);
    /* Output 3, five semitones of 45 rpm trick, full level, filtered side. */
    md12_effect_set(&fx, 2, MD12_TUNE_UNITY - 5, MD12_LEVEL_MAX, MD12_TAP_RING);
}

void audio(const float *in, float *out, int n)
{
    md12_effect_process(&fx, in, out, n);
}
```

Channel numbers count from 0, so channel 2 is output 3. `in` and `out` may be
the same buffer. Call `md12_effect_trigger` whenever a hit should fire the
filter of outputs 1 and 2. `md12_effect_latency` says how many samples the
output lags the input.

### As the machine

```c
static md12_machine sp;
static uint16_t sound[65536];

md12_input in;
md12_outputs o = {0};
int len;

md12_input_init(&in, &cal, 48000, 1, 1);
len = md12_input_process(&in, recording, n, sound, 65536);

md12_machine_init(&sp, &cal, 48000);
md12_machine_trigger(&sp, 0, sound, len, MD12_TUNE_UNITY, MD12_LEVEL_MAX);
o.ring[0] = out1;
o.mix = mix;
md12_machine_process(&sp, &o, block);
```

Sounds are 12-bit codes, stored as the hardware stores them, with 0x800 as
silence. `md12_input` makes them from your audio the way the machine samples.
The input lags by a few samples, so feed it some silence after the recording.
The operating system also writes five silent codes at the start of a fresh
sound and never plays its last five. `tools/md12.c` shows all three. No sound
can be longer than 65,536 samples. Leave a pointer in `md12_outputs` empty
and nothing is written there.
`md12_machine_set_plugged` takes channels out of the mix, as plugging their
jacks does.

## Calibration

Three presets come with it:

- `md12_calib_sp1200`: an SP-1200 as built and trimmed by its service manual.
- `md12_calib_sp12`: an SP-12 in the same way.
- `md12_calib_sp12_measured`: the real SP-12 in John Nolting's recordings.

Each field of `md12_calib` is documented in the header, and each preset in
`src/calib.c` says where its values come from.

Outputs 1 and 2 offer two sweeps, set with `dyn_pin13_ohms`.
`MD12_PIN13_DRAWN` is the filter's control circuit exactly as both service
manuals draw it. `MD12_PIN13_MEASURED` is how the measured SP-12 behaves:
after each hit its brightness ends about 35 ms sooner than the drawing
predicts. Nobody knows why yet, and nobody has measured an SP-1200's. The
presets use the measured sweep.

### Measuring your own machine

If you own an SP-1200 or SP-12, the kit in `tools/kit` measures it. You make
two test sounds, sample them into the machine, record its outputs with a
stereo cable, and get back a calibration file. The procedure is at the top
of `tools/kit/analyse.py`. Recordings of a real SP-1200 would be the most
useful thing anyone could contribute.

```
python3 tools/kit/make_kit.py kit
python3 tools/kit/analyse.py noise.wav tones.wav -o my.cal
```

## The md12 tool

`build/md12` runs WAV files through the library.

```
build/md12 sample drums.wav out.wav -t -5 -c 1 -o ring
build/md12 effect song.wav out.wav -c 3 -o ring -t -8
```

`sample` records the file into the machine and plays it back once; `effect`
runs it through live. The options choose the tuning, channel, level, which
jack to record, the machine, a calibration file, the sweep, the input gain
and the 45 rpm trick. The comment at the top of `tools/md12.c` lists them.
`tools/listen.sh` renders one file through a set of useful variations.

## How it was checked

- **Filters.** Every filter was entered from the schematics as an ngspice
  netlist (`tools/spice`), with each part's designator and drawing page. The
  library matches ngspice within 0.42 dB at 44.1 kHz, 0.29 dB at 48 kHz and
  0.07 dB at 96 kHz. The filter's control circuit matches within 2 percent.
- **Firmware.** The operating system's behaviour comes from running E-mu's
  own code in a Z80 emulator, and agrees across the SP-1200's OS 1.2 and 1.04
  and the SP-12 Turbo's 2.6. The source comments give the ROM addresses.
- **A real SP-12.** `tools/validate_sp12.py` measures Nolting's recordings and
  compares them with the library:
  - The fixed filters have the schematic's shape within 0.22 dB, 6 to 10
    percent higher in frequency.
  - At rest, the dynamic filters are four matched poles, within 0.12 dB.
  - Their sweep matches within 0.024 octaves.
  - Its pitch steps fit the SP-12's rule to 0.23 percent rms.

  Download `sine_sweep_all_ch_tune.wav` and `noise_20k_all_ch.wav` from the
  page below and give the script their folder.
- **Embedded.** `make m7check` builds the test suite for a Cortex-M7 with a
  single-precision FPU and runs it under QEMU. It passes all of the 341
  checks that `make check` runs on a desktop.

## Size and speed

On a Cortex-M7 the library is about 31 KB of code and tables. An
`md12_machine` takes about 4.5 KB of RAM and an `md12_effect` about 2 KB.

On one core of a 2.5 GHz Xeon E5-2680 v3, effect mode takes 100 to 230
nanoseconds a sample, depending on the output's filter. The whole eight
channel machine, with every jack and the mix, takes about 570. Its speed on a
real Cortex-M7 hasn't been measured yet.

## Sources

- E-mu Systems, SP-1200 service manual (1987), schematic SK103, and SP-12
  service manual, S-7600-001.
- E-mu's operating systems: SP-1200 OS 1.2, Rossum's SP-1200 OS 1.04, and the
  SP-12 Turbo's 2.6. The semitone and decay tables are E-mu's, read from
  these.
- The IC88 level PROM and the firmware images as dumped at
  https://github.com/Lytrix/EMU-SP1200
- John Nolting's SP-12 recordings: https://ccrma.stanford.edu/~nolting/220c/
- David T. Yeh, John Nolting and Julius O. Smith, "Physical and Behavioral
  Circuit Modeling of the SP-12 Sampler", ICMC 2007:
  https://ccrma.stanford.edu/~dtyeh/papers/yeh07_icmc_sp12.pdf
- The SSM2044 data sheet (Solid State Micro Technology for Music, 1981), the
  SSI2144 data sheet (Sound Semiconductor), and US patent 4,404,529, Rossum
  et al., "Lowpass filter with electronic control of cutoff and resonance".
- pitcher's SP-1200 pitch ratios: https://github.com/mwcm/pitcher

## Licence

MIT. See `LICENSE`.
