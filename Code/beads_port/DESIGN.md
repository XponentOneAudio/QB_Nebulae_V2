# Porting Mutable Instruments Beads to Qu-Bit Nebulae v2

Status: phase 1 implemented as **Pearls**. See [README.md](README.md) for the
panel, installation and testing. This document is the original proposal.
Where phase 1 differs from it:

- The opcode is `pearls` and the instrument is `pearls.instr`. The upstream
  README asks derivative works not to reuse the module name.
- `libpearls.so` is installed into Csound's plugin directory. `--opcode-lib`
  does not work through the API as Nebulae uses it: the orchestra is compiled
  before `csound.start()`, but option libraries are loaded at start.
- Seed lock follows Beads exactly (tap Reset to unlock, hold 1.5 s to
  re-lock), so the Record button is free. Mono is on Record (secondary), the
  randomness flavour on Reset (secondary) and manual gain on Density
  (secondary).
- The randomness knobs use only the clockwise half of the Beads attenuverter
  law, because Nebulae secondary knobs default to 0. The flavour switch
  gives the counter-clockwise behaviour.
- Gates are polled from `/dev/gpiomem` once per control period. libgpiod with
  edge timestamps is left for later. The EOL output is pulse-stretched in
  wall-clock time because Csound renders in bursts.
- Beads `Processor`/`Synth` got an optional gate-output sink. That makes two
  small upstream edits (see `third_party/eurorack/UPSTREAM.md`).
- No-input detection (below −60 dBFS for 2 s) replaces Beads' normalisation
  probe for the AGC.

## 1. Summary

The port is feasible on the existing Nebulae v2 hardware, with no hardware changes.

- **DSP:** The Beads DSP core (`beads/dsp/*`, about 5k lines of C++ plus stmlib) has no
  STM32-specific code. It builds unmodified with desktop `g++`, and with
  `arm-linux-gnueabihf-g++ -mcpu=cortex-a53` for the Nebulae CPU. Under emulation the
  ARM build runs and produces the same output.
- **CPU:** In its most expensive mode the Beads core needs at most about 3.3 % of one
  2.8 GHz x86 core. Scaled to the Nebulae's Cortex-A53 at 1.2 GHz, that is roughly
  20–40 % of one of its four cores in the worst case (estimate; see §4). Beads was
  designed for a 400 MHz Cortex-M7, and one A53 core is faster than that.
- **Memory:** Beads uses an 832 KB buffer: 768 KB of delay line and 64 KB of reverb.
  On a 1 GB Pi this is negligible.
- **Licence:** Beads is MIT (© Emilie Gillet) and Nebulae is MIT (© Qu-Bit). They are
  compatible. Keep both copyright notices.

The work is not in the DSP. It is in the glue: mapping the Beads panel onto the
Nebulae panel, replacing what the Beads hardware does for free (jack-normalisation
detection, sample-rate switching, gate timing, flash persistence), and LED feedback.

**Recommendation.** Wrap the Beads `Processor` in a host-independent `BeadsEngine`
class. Expose it to Nebulae as a **Csound plugin opcode** (`beads`), driven by a normal
`.instr` file, so it appears in the instrument selector next to the factory
instruments. It then reuses the existing Python control, settings, calibration,
alt-layer and LED code. Gates and the gate output are handled inside the opcode
through direct GPIO access, so they get sample-block timing. The same engine can later
be wrapped as a Pd external or a standalone ALSA host at little extra cost.

## 2. The two platforms

| | Beads (STM32H7) | Nebulae v2 |
|---|---|---|
| CPU | Cortex-M7 @ 400 MHz, single core, bare metal | BCM2837, 4× Cortex-A53 @ 1.2 GHz (`localfiles/config.txt: arm_freq=1200`), Arch Linux ARM |
| Audio path | Interrupt per 12 frames @ 48 kHz (`kBlockRate = 4000`) | Csound 6 via `ctcsound`, ALSA `hw:0,0`; or Pd in "puredata" mode |
| Codec | Sample rate changed per quality mode: 24k / 48k / 32k / 48k | WM8731 (`dtoverlay=rpi-proto`), one rate per Csound instrument (`sr` in `.instr`) |
| Pots / CV | 12 pots: Time, Size, Shape, Pitch, Density, 4 attenuverters, Feedback, Dry/Wet, Reverb. 6 CV inputs (Time, Size, Shape, Pitch, Density, one assignable). Dedicated ADCs | 6 pots + 2 push-encoders (Speed, Pitch), 8 CV inputs; 2× MCP3208 read from Python over SPI |
| Gates | Freeze and Seed gates sampled **per audio frame** together with the codec | Freeze (GPIO 4), Reset (25), Next (23), Record (24) on GPIO; Source gate via CD4021 shift register; polled from the Python loop |
| Gate out | Optional: right output becomes grain gate | Dedicated EOL gate out (GPIO 16) |
| Buttons | Seed, Freeze, Quality (media), Assign | Record, Next, Source, Freeze, Reset |
| LEDs | Quality RGB, Seed, Freeze, 3× assign, VU | 2× bipolar RGB pairs (Speed, Pitch) + Record/Next/Source/Freeze (PCA9685), Reset (GPIO 12) |
| Jack detection | Normalisation probe on every input | **None** |
| Persistence | Settings + frozen buffer backed up to internal flash | SD card (normally read-only, `mountfs.sh rw/ro`) and USB stick |

### What Beads does that Nebulae cannot do directly

1. **Jack-normalisation detection** (`normalization_detector.cc`). Beads uses it for:
   - mono vs. stereo input (mono doubles the buffer length);
   - automatic synth mode after 10 s with no audio patched;
   - whether the Seed gate input is patched (external triggers replace the internal generator);
   - whether each Time/Size/Shape/Pitch CV is patched. This matters: the
     attenuverter means "CV amount" when a CV is patched, and "amount of internal
     randomisation" when it is not (`RandomizedParameter::value`).
2. **Hardware sample-rate changes.** The WM8731 with a 12.288 MHz MCLK supports 48 k
   and 32 k but **not 24 k**. Csound also fixes `sr` per orchestra.
3. **Per-frame gate sampling.** Nebulae reads gates in Python. The jitter is several
   milliseconds and depends on the loop rate.

Each is handled in §5.

## 3. Feasibility evidence

`feasibility/` contains a benchmark harness (`bench.cc`) and a build script. The
benchmark drives `beads::Processor` / `beads::Synth` with these worst-case settings:

- density 0.95, so up to 32 overlapping grains;
- randomisation on all four parameters;
- feedback 0.5, reverb 0.6;
- each of the four quality modes, mono and stereo, granular mode (size 0.6) and
  delay/tap mode (size 1.0);
- block sizes of 24, 64 and 128 frames.

Steps run:

1. `pichenettes/eurorack@HEAD`, stmlib submodule at `e3bd7c9`, `beads/test/makefile`.
   This builds with current `g++` after two trivial changes: drop `-Werror` (modern
   gcc warns on stmlib's `STATIC_ASSERT`) and add `-include cstdio` (a missing include
   in `wavetable_granular_player.h`).
2. Cross-built for the Nebulae CPU with `arm-linux-gnueabihf-g++ -O2 -mcpu=cortex-a53
   -mfpu=neon-fp-armv8 -mfloat-abi=hard`. No warnings.
3. Ran the ARM binary under `qemu-arm`. Delay/tap-mode output matches the x86 build to
   5–6 significant digits. Granular output is statistically equivalent but not
   identical. This is expected: grain scheduling is stochastic, and tiny libm
   differences change the trigger times.

Measured CPU, as % of one Xeon core @ 2.8 GHz, 20 s of audio per run:

| Mode (codec rate) | granular, stereo | granular, mono | delay/tap, stereo | delay/tap, mono |
|---|---|---|---|---|
| Dirty tape (24 k, 8-bit µ-law) | 0.6 % | 0.5 % | 1.5 % | 1.2 % |
| Clean tape (48 k, internal 2:1 SRC) | 1.1 % | 1.0–1.3 % | 2.7–2.9 % | 2.2–2.8 % |
| Vintage digital (32 k, 12-bit) | 0.7 % | 0.7–0.9 % | 2.4–2.7 % | 2.1–3.3 % |
| Digital (48 k, 16-bit) | 0.9–1.4 % | 0.9–1.3 % | 2.6–3.3 % | 2.2–2.6 % |
| Wavetable synth mode (48 k) | 1.0–1.2 % | – | – | – |

Block size (24, 64 or 128 frames) has no significant effect on cost.

## 4. CPU estimate for the Nebulae

This is not measured on device yet; Phase 0 below does that.

There are two independent estimates:

- **From the x86 benchmark.** An in-order A53 at 1.2 GHz is roughly 6–12× slower than
  the benchmark host on scalar float code with scattered memory access. The 768 KB
  buffer is larger than the A53's 512 KB L2. That gives about 20–40 % of one core in
  the worst case (stereo tap-delay) and 5–15 % in granular mode.
- **From the original target.** Beads runs within the budget of a 400 MHz Cortex-M7
  with single-cycle SRAM. One A53 at 1.2 GHz has roughly 1.5–2.5× the throughput,
  so under 70 % of one core even if Beads were close to 100 % on the H7.

Either way, the Beads core leaves headroom on one of four cores. The existing
`a_granularlooper` instrument already runs `setksmps 1` grain code in Csound on this
hardware.

## 5. Proposed design

### 5.1 Software architecture

```
┌──────────────── nebulae.py (unchanged main loop) ───────────────┐
│ ControlHandler: pots/CV/encoders → Csound channels (chnexport)  │
│ UI: LEDs, alt layer, instrument selector                        │
└───────────────┬─────────────────────────────────────────────────┘
                │ gkdensity, gksize, …, gkfreeze, gkreset, …
┌───────────────▼──────────── beads.instr (Csound orchestra) ─────┐
│ maps Nebulae channels → Beads parameters, calls:                │
│   aL, aR, kstatus  beads  ainL, ainR, kparams...                │
└───────────────┬─────────────────────────────────────────────────┘
                │ Csound plugin opcode (libbeads_nebulae.so)
┌───────────────▼─────────────────────────────────────────────────┐
│ BeadsEngine (host-independent C++)                              │
│  - splits each ksmps block into 12-frame sub-blocks (= Beads    │
│    kBlockRate) so parameter interpolation matches the original  │
│  - GateReader: GPIO 4/25/23/24 via libgpiod edge events with    │
│    timestamps → per-frame freeze[] / seed[] arrays              │
│  - GateWriter: grain gate → GPIO 16 (EOL jack)                  │
│  - RateAdapter: 48 k host ↔ 32 k / 24 k internal processing     │
│  - Port of gain_control.cc (AGC + tape soft-clip)               │
│  - Patch-state heuristics replacing normalisation detection     │
│  - beads::Processor / beads::Synth (upstream, unmodified)       │
└─────────────────────────────────────────────────────────────────┘
```

The upstream sources are vendored unmodified, either as a git subtree of
`pichenettes/eurorack` (`beads/dsp`, `beads/resources.*`) or as a copy with a pinned
commit. A small `beads_nebulae/` directory then holds `engine.{h,cc}`, `gates.{h,cc}`,
`rate_adapter.h`, `csound_opcode.cc` and a makefile.

### 5.2 Why a Csound opcode as the first host

| Host | Pros | Cons |
|---|---|---|
| **Csound plugin opcode** (recommended) | Shows up as an ordinary instrument (factory or user bank). Reuses Python control, settings persistence, alt layer, calibration and LED code. `.instr` config already sets button/gate behaviour (latching/triggered/incremental, edges). Deploys as one `.so` plus one `.instr`. | Csound renders in bursts of `-b` frames, so in-opcode GPIO polling would be bursty. Solved with timestamped GPIO edge events (§5.4). |
| Pd external `beads~` | Pd mode already exists; Python sends controls over TCP 3000 (`pdsender.py`). Users can patch around it. | Pd mode has a thinner UI path (no instr-specific config, LEDs). |
| Standalone ALSA process | Full control of RT thread, rate and timing | New "bank" type in `nebulae.py`, and LED/control state must be bridged both ways. Most code to write. |

Because `BeadsEngine` is host-independent, the Pd external and a desktop test host
(WAV in, WAV out) are thin wrappers. The desktop host also makes the engine testable
in CI without hardware.

### 5.3 Control mapping

Beads has eight "main" controls, and so does Nebulae (6 pots + 2 encoders), so the
primary layer maps one-to-one.

**Primary layer**

| Nebulae control (+ its CV jack) | Beads parameter | Notes |
|---|---|---|
| Start | TIME | CV summed into base (Beads: ±1.6 range, 0.5 scale) |
| Size | SIZE | Fully CW (> 0.99) switches to delay mode, as on Beads |
| Window | SHAPE | |
| Density | DENSITY | Centre = no grains; CW = random, CCW = regular (Beads law). CV → `d.cv`, including Beads' expo-FM split |
| Pitch encoder + 1 V/oct CV | PITCH | Encoder → Beads `LAW_DETUNE` curve (±24 st with 0/1/3/4/7/12/24 st detents). Nebulae's calibrated pitch CV → semitones into `pitch.cv`. Encoder click toggles fine/coarse, as now |
| Overlap | FEEDBACK | Overlap CV = feedback CV |
| Blend | DRY/WET | Blend CV = dry/wet CV |
| Speed encoder | REVERB | Speed CV = reverb CV. LED ring shows amount |

All three Beads mix parameters get their own CV. On Beads they share one "assign" CV,
so the port is a superset there.

**Secondary layer (hold Source, existing Nebulae mechanism)**

| Nebulae alt control | Beads function |
|---|---|
| Start_alt | TIME attenuverter: CCW = per-grain random, centre = none, CW = smooth random walk |
| Size_alt | SIZE attenuverter (same law) |
| Window_alt | SHAPE attenuverter |
| Pitch_alt (encoder) | PITCH attenuverter |
| Speed_alt | Input level (unchanged Nebulae behaviour via `setInputLevel`) |
| Density_alt, Overlap_alt, Blend_alt | Spare. Candidates: seed-gate patch timeout, extended buffer length, wow/flutter depth |
| Freeze_alt (button) | Toggle autosave of the frozen buffer (Beads: Assign+Freeze) |
| Record_alt | Mono / stereo buffer (replaces Beads' auto-detection; mono = 2× buffer time) |
| Reset_alt | Monophonic grain mode (Beads `ENABLE_ADVANCED_FEATURES`) |
| Next_alt (file_alt) | Reserved for Phase 3 (load USB sample into buffer) |

**Buttons and gates (primary)**

| Nebulae | Beads | `.instr` config |
|---|---|---|
| Freeze button / Freeze gate | FREEZE (button latches; gate is momentary-OR, as on Beads) | `freeze,latching,rising`; the gate is read in-engine |
| Reset button / Reset gate | SEED (tap = grain, or tap-tempo in delay mode; gate = external seed) | `reset,triggered,rising` |
| Record button / Record gate | Seed **lock** toggle (Beads: Seed+Freeze). LED pulses when locked | `record,latching,rising` |
| Next button / Next gate | QUALITY cycle: dirty tape → clean tape → vintage → digital. The gate makes quality switchable by CV | `file,triggered,falling`; counter kept in the orchestra |
| Source tap / Source gate | Live input ↔ wavetable **synth mode** (Beads' "nothing patched" mode) | `source,latching,falling` |
| Source hold | Secondary layer (unchanged) | |
| EOL gate out | Grain / delay-tap gate out (Beads `gate_out[]`), driven from the engine | |

The Beads trait that changing quality clears the buffer is kept.
`Processor::Configure()` calls `Reset()`.

### 5.4 Replacing Beads hardware features

**Attenuverter semantics without jack detection.** For Time, Size, Shape and Pitch,
pass `has_cv = false` and add the Nebulae CV to `base`. This is what Nebulae does for
every other instrument. The alt-layer knob then always means "internal randomisation"
with Beads' exact law. This loses only Beads' "attenuate the patched CV" behaviour,
which Nebulae users get from external attenuators.

**Seed input "patched" detection.** The engine considers the Seed (Reset) gate
patched from its first rising edge until 4 s pass without an edge. While patched it
passes the `seed[]` array, so external triggers replace the internal generator, as
on Beads. The timeout is configurable (candidate: Density_alt).

**Mono/stereo and synth mode.** These are explicit settings: Record_alt and Source.
Optionally, synth mode can also auto-engage after 10 s of input below −60 dBFS,
mirroring Beads.

**Sample rates.** The host runs at a fixed 48 kHz (`sr,48000`).
- *Dirty tape:* compile with `ENABLE_HARDWARE_24K` undefined. This upstream code
  path already runs dirty tape at 48 kHz with the internal 2:1 SRC
  (`GranularProcessor<…, ratio = 2>`), so no new DSP is needed.
- *Vintage digital (32 kHz):* add a 3:2 polyphase resampler (48 k → 32 k → 48 k) in
  `RateAdapter`, around the processor only, using `speexdsp` (BSD) or a fixed ~48-tap
  half-band/polyphase FIR. Its aliasing and band-limiting is part of the
  "vintage" character, so A/B it against a real Beads.
- Alternative: reopen ALSA at 32 kHz like Beads does. This is not possible inside
  Csound without recompiling the orchestra. Keep it for the standalone host.

**Gate timing.** Python polling gives multi-millisecond jitter, and Csound computes
`-b` frames in bursts. The engine therefore opens the four gate GPIOs with
`libgpiod` edge events, which carry kernel timestamps. On each ksmps it maps the
events since the last call onto frame offsets, using the audio clock
(`CLOCK_MONOTONIC` versus frames rendered, re-anchored each block). This gives a
constant latency of about one period with sub-millisecond jitter, which is good
enough for Beads' clocked delay mode and its 2.5 ms seed delay. Python keeps reading
the same pins for its own button/LED logic; read-only sharing is harmless. Use
`-b 128 -B 512` (2.7 ms periods) in `beads.instr`.

**Gate output.** The engine sets GPIO 16 from `gate_out[]`, so Python no longer
drives EOL for this instrument (`gkeol` stays 0).

**Gain.** Port `gain_control.cc`: AGC, VU and the media-dependent soft-clip of
dirty/clean tape. It only depends on the `Codec::Frame` type, which needs a 5-line
shim. Nebulae's hardware input level (Speed_alt) stays in front of it.

**Persistence.** Settings (quality, mono, seed lock, attenuverters) use the existing
`settings.py` channel persistence. The frozen-buffer backup is Phase 3. On freeze,
when autosave is on, a worker thread writes the 768 KB buffer
(`Processor::delay_buffer_raw_bytes`) to `/home/alarm/beads_buffer.bin` inside the
existing `mountfs.sh rw/ro` bracket. On load it restores the buffer and starts
frozen, as Beads does.

### 5.5 LED feedback (small `ui.py` patch)

`ui.py` already special-cases `a_granularlooper`. Add a `beads` branch that reads
three new output channels exported by the orchestra (`chnexport … , 2`):

- `beads_quality` (0–3): for 1.5 s after a Next press, both RGB pairs show Beads' media
  colour (`media_palette_`: magenta, amber, teal, white).
- `beads_seed_led`: Record LED; pulsing triangle when seed-locked, grain flashes when unlocked.
- `beads_reserve`: Freeze LED brightness from `overwrite_reserve()` (the buffer-fill
  fade Beads shows on its quality LED).

Speed and Pitch rings keep their normal behaviour (reverb amount, pitch).

### 5.6 `beads.instr` sketch

```csound
nebconfigbegin
ksmps,64
-b,128
-B,512
sr,48000
freeze,latching,rising
reset,triggered,rising
record,latching,rising
file,triggered,falling
source,latching,falling
nebconfigend

gkquality init 3
instr 1
  ainL, ainR ins
  if changed2(gkfilesel) == 1 then
    gkquality = (gkquality + 1) % 4
  endif
  aL, aR, kseedled, kreserve beads ainL, ainR, \
      gkloopstart, gkloopsize, gkwindow, gkdensity, \   ; time, size, shape, density
      gkpitch, gkspeed, gkoverlap, gkblend, \            ; pitch, reverb, feedback, dry/wet
      gkloopstart_alt, gkloopsize_alt, gkwindow_alt, gkpitch_alt, \
      gkfreeze, gkreset, gkrecord, gkquality, gksource, gkrecord_alt, gkreset_alt
  chnset gkquality, "beads_quality"
  chnset kseedled, "beads_seed_led"
  chnset kreserve, "beads_reserve"
  outs aL, aR
endin
```

The exact scaling of `gkpitch` (0–1 = 5 octaves after Nebulae calibration) and
`gkspeed` (encoder range) to Beads units is done in the opcode, so the `.instr` stays
readable.

### 5.7 Build and deploy

- Build on device (Arch ARM `armv7h`, the same way `Code/pdexternals` are built) or with
  a cross toolchain. Flags: `-O2 -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
  -DTEST -include cstdio`. `-DTEST` selects the non-embedded stmlib paths.
- Install `libbeads_nebulae.so` into Csound's plugin directory (`OPCODE6DIR`), or add
  an `--opcode-lib` key to the `.instr` config, which needs a 3-line `nebulae.py` change.
- Ship through the existing firmware-update zip (`create_fw_update.sh`, `update.sh`).
- Pin the Csound performance thread to one core with `SCHED_FIFO` (via the existing
  `--realtime` option). Optionally isolate a core with `isolcpus=3`.

## 6. Plan

| Phase | Work | Exit criterion | Rough effort |
|---|---|---|---|
| 0 – On-device spike | Run `feasibility/build_bench.sh` on a Nebulae. Confirm the Csound version and plugin loading. Check WM8731 rates on the rpi-proto driver. | Worst-case Beads CPU < 50 % of one core; a "hello" opcode loads | 1–2 days |
| 1 – Engine + opcode | `BeadsEngine`, Csound opcode, `beads.instr`, all four modes at 48 k (3:2 SRC for vintage), synth mode, primary/alt mapping. Desktop WAV test host. | Plays on hardware with no xruns at max density in every mode | 1–2 weeks |
| 2 – Timing and feedback | libgpiod gate reader with timestamps, GPIO 16 gate out, seed patch heuristic, gain control port, `ui.py` LED branch, settings persistence | Delay mode locks to an external clock; LEDs match Beads semantics | ~1 week |
| 3 – Nebulae-native extras | Frozen-buffer autosave/restore. Load a USB sample into the buffer (Next_alt). Write the buffer to WAV (reuse `wavewriter.py`). `beads~` Pd external. | – | ~1 week |

## 7. Risks and open questions

| Risk | Likelihood | Mitigation |
|---|---|---|
| A53 slower than estimated (cache misses on the 768 KB buffer) | Low | Phase 0 measures it first. Fallbacks: `-O3`/LTO, 64-bit build, reduce `NUM_GRAINS` from 32 |
| Xruns from SD/USB I/O or the Python loop | Medium | `SCHED_FIFO`, core pinning, `-B 512`; buffer saves on a low-priority thread |
| Gate timing via libgpiod in a Csound plugin (threading inside the opcode) | Medium | The reader thread only queues timestamps; the opcode drains the queue lock-free. Fallback: a Pd/standalone host with ALSA period timestamps |
| Vintage-digital character differs with software 3:2 SRC | Medium | A/B against hardware Beads; tune the filter or reopen ALSA at 32 k in the standalone host |
| Nebulae's 5 buttons cannot express every Beads combo (calibration, factory test) | Low | Not needed: Nebulae has its own calibration (`check_calibration.py`) |
| Csound version on the shipped image lacks something the opcode needs | Low | Use the plain C opcode API (`csdl.h`), available since Csound 5 |
| Naming / trademark | – | Code is MIT, but "Mutable Instruments" and "Beads" are product names. Ship it under a distinct instrument name (e.g. "Pearls") with credit to Emilie Gillet in the instr file and docs |

## 8. Reproducing the feasibility numbers

```sh
git clone --recursive https://github.com/pichenettes/eurorack.git
cd QB_Nebulae_V2/Code/beads_port/feasibility
./build_bench.sh /path/to/eurorack           # native (desktop or on the Nebulae)
./beads_bench                                # CPU % of one core, per mode/block size
./beads_bench check                          # output sanity check across targets
CXX=arm-linux-gnueabihf-g++ EXTRA=-static ./build_bench.sh /path/to/eurorack   # cross build
```
