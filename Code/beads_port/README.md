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
> way the Nebulae firmware does, and an ARM cross-build of the engine tests. The
> prebuilt ARM plugin has also passed the full instrument test in an emulated
> 2018 ARM system (Ubuntu 18.04 armhf: Csound 6.10, glibc 2.27). The first run
> on real hardware is the next step. See [Testing on the
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

## Installing with a USB stick (no SSH)

This uses the Nebulae's normal firmware-update mechanism. The update is the
official v2.1.2 firmware plus Pearls (version `v2_1_2-pearls1`).

> **Check your hardware first.** This firmware is for Nebulae v2 units with a
> **Raspberry Pi 3** on the back. A Pi 3 has full-size HDMI and a micro-USB
> power socket; a Pi 4 has two micro-HDMI ports and USB-C. If yours has a
> Pi 4, don't install this yet.

1. Copy `neb_update.zip` to the **root of your USB stick**. Your audio files
   and instruments can stay on it. Get the file from
   `FirmwareUpdates/v_2_1_2_pearls1/` in this repository, or make it with
   `./create_fw_update.sh`.
2. With the stick inserted, power the Nebulae on. It installs the update
   (the LEDs show the update animation) and reboots by itself. It removes
   `neb_update.zip` from the stick when it's done.
3. On the next boot, Pearls installs itself before Nebulae starts. This adds a
   few seconds, once. It then writes **`pearls_install_log.txt`** to the
   stick. The log ends with either "Instrument installed" or "Pearls is NOT
   installed. Nebulae works as before." and the reason.
4. Open the instrument selector (hold the Speed encoder for 1.5 s) and choose
   `pearls` in the **user** bank.

What happens on the module:
- `boot_install.sh` copies the prebuilt plugin (`prebuilt/armv7/libpearls.so`)
  into Csound's plugin directory.
- It checks that the plugin really loads and runs in the module's own Csound
  (`tools/verify_plugin.py`).
- If the check fails and the module has a compiler, it compiles the plugin
  there instead.
- It never stops the Nebulae from booting, and it doesn't retry a failed
  install until new firmware arrives.
- If your stick holds other `.instr` files, it also copies `pearls.instr`
  there. Otherwise Nebulae would replace the user bank with the stick's
  instruments and Pearls would disappear.

**Going back** to the official firmware works the same way. Put the official
`FirmwareUpdates/v_2_1_2/neb_update.zip` on the stick and boot. The plugin
file in Csound's plugin directory stays but is unused.

## Installing over SSH (building on the module)

For development, or if the prebuilt plugin doesn't work on your module.
You need SSH access (Ethernet on the Pi, user `alarm`, password `alarm`) and
this firmware on the module (see above, or clone this branch into
`/home/alarm/QB_Nebulae_V2`).

```sh
ssh alarm@<nebulae-address>
cd /home/alarm/QB_Nebulae_V2/Code/beads_port
./install.sh
```

This builds `libpearls.so` on the module and runs the tests, which takes a
few minutes. It then installs the plugin and `pearls.instr`. If `g++` is
missing: `sudo pacman -S gcc make`. Remove with `./install.sh --uninstall`.

### Firmware changes in this update

All are inactive for other instruments:
- `controlhandler.py`: config keys that let an instrument read gate jacks
  itself (`ignoregate`) and drive the EOL jack (`eol,engine`).
- `ui.py`: Pearls LED feedback (`ui,pearls`).
- `nebulae.py`: if an instrument's orchestra fails to compile, Nebulae falls
  back to the Granular Looper instead of failing at every boot.
- `scripts/startup.sh`: runs `boot_install.sh`.

## Trying it on a computer with the Qu-Bit Instr Tester

Qu-Bit's [Instr Tester](https://www.qubitelectronix.com/alternate-firmware/p/p6o97gn5ox514np90lisz29gxzikqx)
(also in this repo as `Instruments/Instr Tester/Neb_Instr_Tester.zip`) runs
`.instr` files on a Mac or Linux computer, with sliders in place of the knobs.
It needs Python 2.7, NumPy, Tkinter and Csound 6. Pearls also needs its plugin
built for your computer:

1. Install Csound 6 with its headers. On Debian/Ubuntu that's `csound` and
   `libcsound64-dev`. On a Mac, install the Csound package; you also need
   Xcode's command-line tools.
2. Build the plugin: `make` in this folder. This gives `build/libpearls.so`,
   or `build/libpearls.dylib` on a Mac. The Mac build is untested.
3. Copy the plugin into Csound's plugin directory. On Linux that's
   `/usr/lib/x86_64-linux-gnu/csound/plugins64-6.0` or
   `/usr/local/lib/csound/plugins64-6.0`. On a Mac it's
   `/Library/Frameworks/CsoundLib64.framework/Versions/6.0/Resources/Opcodes64`.
   The tester, like the module, compiles the orchestra before starting Csound,
   so an `--opcode-lib` option would not work.
4. Copy `pearls.instr` into the tester's `instr/` folder. Then, in the tester
   folder, run `python instr_tester/nebulae.py pearls`.

In the tester:
- **Next** steps the quality on each press, except when the tester's file
  index wraps around. Put a few audio files in its `audio/` folder; with
  fewer than two, Next does nothing.
- **Reset** is a short tap, so it seeds grains and unlocks the generator.
  Holding it to re-lock isn't possible there.
- **Secondary buttons** (mono buffer, randomness flavour) aren't available yet.
- **Gate jacks and the EOL output** don't exist on a computer. A
  `GPIO gate access unavailable` warning is expected.

`tools/csound_harness.py --conductor <tester>/instr_tester/conductor.py
--next-style increment` checks `pearls.instr` against the tester's orchestra
header without the GUI.

## Testing on the Nebulae

Things to check on the first run. The commands need SSH; the listening tests
don't.

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
- **Logs.** `pearls_install_log.txt` on the stick shows the install. Over SSH,
  `journalctl -u nebulae` shows Csound messages. A warning saying
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
control names are listed at the top of `tools/pearls_render.cc`.

Cross-compiling for the Nebulae: `make CXX=arm-linux-gnueabihf-g++
BUILD=build-arm`.

The portable binary for USB installs is built by `make prebuilt`
(`tools/build_prebuilt.sh`). It compiles against the Csound 6.09 headers and
refuses to produce a binary that needs newer system libraries than the Nebulae
has. To test that binary in a real ARM Csound under qemu, unpack Ubuntu
18.04 armhf `csound` packages into a directory and run:

```sh
python3 tools/csound_harness.py --no-chnexport --plugin-dir prebuilt/armv7 \
    --csound "qemu-arm -L ROOT ROOT/usr/bin/csound"
```

`tools/verify_plugin.py [plugin_dir]` is the load-and-run check used on the
module at boot.

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
boot_install.sh         installs the prebuilt plugin at boot (USB install)
install.sh              builds and installs on the module (SSH install)
prebuilt/armv7/         portable plugin binary (tools/build_prebuilt.sh)
tools/                  tests, renderer, benchmark, Csound harnesses,
                        plugin check
third_party/eurorack/   Beads DSP + stmlib, pinned (see UPSTREAM.md)
feasibility/            the original feasibility benchmark
DESIGN.md               design notes
```

## Credits

- Beads DSP and stmlib: Emilie Gillet, Mutable Instruments (MIT licence).
- Nebulae v2 firmware: Qu-Bit Electronix (MIT licence).

This port has no affiliation with Mutable Instruments or Qu-Bit Electronix.
