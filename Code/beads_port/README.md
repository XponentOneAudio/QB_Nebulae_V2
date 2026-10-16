# Pearls: Beads on the Nebulae v2

Pearls is an unofficial, non-commercial port of the **Mutable Instruments
Beads** granular processor to the **Qu-Bit Nebulae v2**. It is made for
personal use and for sharing in the spirit of open source.

The audio code is Emilie Gillet's original Beads DSP (MIT licence), unchanged
apart from two small hooks. Around it, a new engine stands in for the Beads
hardware, a Csound plugin opcode runs it, and a Nebulae instrument file maps
the panel. The upstream README asks derivative works not to use the original
name, hence "Pearls".

> **Status: first port, not yet tested on a Nebulae.** Everything below has been
> built and tested on a desktop. That includes Csound running the instrument the
> way the Nebulae firmware does, plus an ARM cross-build of the engine tests.
> The first run on real hardware is the next step. See [Testing on the
> Nebulae](#testing-on-the-nebulae).

## What you get

- All four Beads quality settings, granular and delay modes, freeze, the
  seed button and seed input (including clock division), reverb, feedback,
  dry/wet, the internal randomisation, and the wavetable synth mode.
- The Beads automatic input gain control, plus a manual gain option.
- Grain gates on the Nebulae EOL output, with stereo audio kept.
- Separate CV for feedback, dry/wet and reverb (Beads has one assignable CV
  for these).

## Panel

| Nebulae | Pearls (Beads) |
|---|---|
| **Start** knob + CV | TIME |
| **Size** knob + CV | SIZE. Fully clockwise: delay mode |
| **Window** knob + CV | SHAPE |
| **Density** knob + CV | DENSITY. 12 o'clock: no grains; clockwise: random; counter-clockwise: regular |
| **Pitch** encoder + 1 V/oct CV | PITCH (unison where the Nebulae's own unison is) |
| **Overlap** knob + CV | FEEDBACK |
| **Blend** knob + CV | DRY/WET |
| **Speed** encoder + CV | REVERB. Turn clockwise from speed 0; clicking the encoder toggles a light reverb on and off |
| **Freeze** button | FREEZE (toggles) |
| **Freeze** gate | FREEZE while the gate is high (as on Beads) |
| **Reset** button | SEED: tap for a grain (or tap tempo in delay mode); hold 1.5 s to re-lock the grain generator |
| **Reset** gate | SEED input: triggers or clocks grains. Seen as patched while it receives edges (4 s timeout) |
| **Next** button / gate | QUALITY: dirty tape → clean tape → vintage digital → digital. The encoder LEDs show the Beads colour for 1.5 s |
| **Source** button / gate | Wavetable synth mode on/off |
| **EOL** output | Grain / delay-tap gate |
| **Reset** LED | Grain activity (dim while locked and idle) |

**Secondary layer** (hold Source, as for other Nebulae instruments):

| Nebulae | Pearls |
|---|---|
| Start / Size / Window | TIME / SIZE / SHAPE randomness (fully CCW = none) |
| Pitch encoder | PITCH randomness |
| Density | Input gain: fully CCW = automatic (default), otherwise 1×–40× manual |
| Speed encoder | Nebulae input level (unchanged) |
| Reset | Randomness flavour: off = smooth variations, on = sparse jumps |
| Record | Mono buffer: left input only, twice the recording time |

The Record button is not used in the primary layer. Holding it still gives
the usual Nebulae edit combos, such as Record + Source for the instrument
selector.

### Differences from a real Beads

- **No jack detection** (the Nebulae has none):
  - Mono vs. stereo is chosen on the secondary layer.
  - Synth mode is the Source button.
  - The seed input counts as patched while it receives triggers.
  - With no audio input (below −60 dBFS for 2 s), the input is treated as
    unpatched, so the automatic gain control doesn't boost the noise floor.
- **Attenuverters:** CVs are added to the knobs, as on all Nebulae
  instruments. The four "attenuverters" therefore always set the amount of
  internal randomisation, which is what they do on Beads with nothing
  patched.
- **Sample rate:** the codec stays at 48 kHz.
  - Dirty tape uses Beads' own 48 kHz + 2:1 resampling path.
  - Vintage digital runs at 32 kHz through a polyphase resampler.
- **Not persistent:** the quality setting resets to digital after a reboot, and
  the frozen buffer is not saved to storage yet.
- **Gate timing:** gates are read and written once per Csound control period,
  about 1.3 ms with Csound's rendering bursts on top. CVs go through the
  Nebulae's Python control loop, so a CV that changes together with a seed
  trigger may land a few milliseconds late.

## Installing on the Nebulae

You need SSH access to the Nebulae (as for Pd patches) and this branch of
the firmware on it.

1. **Put this firmware on the Nebulae.** Either:
   - make an update file: run `./create_fw_update.sh` from a checkout of this
     branch, copy `neb_update.zip` to the root of your USB stick, and boot the
     Nebulae with it plugged in (your old firmware is kept in
     `/home/alarm/backup/old`); or
   - clone it over SSH into `/home/alarm/QB_Nebulae_V2`. Remount read-write
     first (`sh /home/alarm/QB_Nebulae_V2/Code/scripts/mountfs.sh rw`) and move
     the old folder out of the way.

   This firmware has three small changes in `Code/nebulae/`, all inactive for
   other instruments:
   - generic config keys that let an instrument read gate jacks itself
     (`ignoregate`) and drive the EOL jack (`eol,engine`);
   - Pearls LED feedback (`ui,pearls`);
   - a safety net: if an instrument's orchestra fails to compile (for
     example, when its plugin is missing), Nebulae falls back to the Granular
     Looper instead of failing at every boot.

2. **Build and install** (takes a few minutes, mostly the tests):

   ```sh
   ssh alarm@<nebulae-address>
   cd /home/alarm/QB_Nebulae_V2/Code/beads_port
   ./install.sh
   ```

   This builds `libpearls.so` and copies it into the Csound plugin directory.
   That's outside the firmware folder, so it survives firmware updates. It also
   copies `pearls.instr` to `/home/alarm/instr`. If `g++` is missing, install
   it with `sudo pacman -S gcc make`.

3. **Select it.** Hold the Speed encoder for 1.5 s to open the instrument
   selector and choose `pearls` in the user bank.

> **USB sticks with instruments:** if your stick has `.instr` files in its
> root, Nebulae replaces `/home/alarm/instr` with them when an instrument
> loads. Copy `pearls.instr` onto the stick as well.

To remove: `./install.sh --uninstall`.

## Testing on the Nebulae

Things to check on the first run:

```sh
cd /home/alarm/QB_Nebulae_V2/Code/beads_port
make test                     # engine tests (71 checks)
make bench && build/pearls_bench   # CPU use per mode; stop Nebulae first
```

- **CPU.** `pearls_bench` needs less than about 50% in the worst case (one
  core). On a desktop the worst case is 3%.
- **Audio.** Listen for clicks or dropouts at high density and in delay
  mode. If there are any, raise `-B` (then `-b`) in `pearls.instr`.
- **Controls.** Gates on Freeze and Reset, the EOL output, Next cycling through
  the four colours, and synth mode on Source.
- **Logs.** `journalctl -u nebulae` shows Csound messages. A warning saying
  `pearls: GPIO gate access unavailable` means the gate jacks are not being
  read.

## Developing on a desktop

Requires `g++`, `make`, and for the opcode the Csound headers. On Debian or
Ubuntu, install `libcsound64-dev`; also install `csound` and `python3-csound`
for the harnesses.

```sh
make test                      # engine tests
make                           # build/libpearls.so (Csound opcode)
python3 tools/csound_harness.py      # pearls.instr in csound, offline
python3 tools/csound_api_harness.py  # same, through the API like nebulae.py
make render
build/pearls_render -i loop.wav -o out.wav density=0.8 size=0.4 freeze=1@5 quality=0@8
```

`pearls_render` takes Nebulae control values (`name=value@seconds`). The
control names are listed at the top of `tools/pearls_render.cc`. To cross-compile for the Nebulae:
`make CXX=arm-linux-gnueabihf-g++ BUILD=build-arm`.

## Layout

```
pearls.instr            Nebulae instrument (control mapping, Next counter)
engine/                 host-independent engine
  pearls_engine.*         replaces beads.cc / ui.cc hardware glue
  nebulae_mapping.h       Nebulae control values -> Beads parameters
  gain_control.*          Beads AGC, adapted to float input
  rational_resampler.h    48 kHz <-> 32 kHz for vintage digital
  rpi_gpio.*              gate jacks via /dev/gpiomem
csound/pearls_opcode.cc Csound plugin opcode "pearls"
tools/                  tests, renderer, benchmark, Csound harnesses
third_party/eurorack/   Beads DSP + stmlib, pinned (see UPSTREAM.md)
feasibility/            the original feasibility benchmark
DESIGN.md               design notes
```

## Credits

- Beads DSP and stmlib: Emilie Gillet, Mutable Instruments (MIT licence).
- Nebulae v2 firmware: Qu-Bit Electronix (MIT licence).

This port has no affiliation with Mutable Instruments or Qu-Bit Electronix.
