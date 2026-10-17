# Pearls: Beads on the Nebulae v2

Pearls is an unofficial, non-commercial port of the **Mutable Instruments
Beads** granular processor to the **Qu-Bit Nebulae v2**. It is made for
personal use and for sharing in the spirit of open source.

The audio code is Emilie Gillet's original Beads DSP (MIT licence), unchanged
apart from a few small hooks (listed in `third_party/eurorack/UPSTREAM.md`). Around it, a new engine stands in for the Beads
hardware, a Csound plugin opcode runs it, and a Nebulae instrument file maps
the panel. The upstream README asks derivative works not to use the original
name, hence "Pearls".

> **Status: working on hardware; new features untested on hardware.**
> Firmware `v2_1_2-pearls4` installs from a USB stick and runs Pearls on a
> Nebulae v2 (Raspberry Pi 3, Csound 6.11), confirmed in hands-on testing.
> `v2_1_2-pearls5` added:
> - loading samples from the USB stick and saving the buffer to it;
> - memory across power cycles;
> - tap tempo, reversed grains and stereo spread;
> - tighter gate timing.
>
> `v2_1_2-pearls6` brings the controls closer to the Beads manual. The seed
> re-lock and the freeze handling now work as on Beads, the quality settings
> use the manual's names and order, and holding Next restarts the input gain.
>
> These new features have passed the desktop tests but not yet a hands-on test.
> The tests cover the engine (desktop and ARM), Csound (including an emulated
> 2018 ARM Csound), and a simulator of the Nebulae Python program.

## What you get

- All four Beads quality settings, granular and delay modes, freeze, the
  seed button and seed input (including clock division), reverb, feedback,
  dry/wet, the internal randomisation, and the wavetable synth mode.
- The Beads automatic input gain control, plus a manual gain option.
- Grain gates on the Nebulae EOL output, with stereo audio kept.
- Separate CV for feedback, dry/wet and reverb (Beads has one assignable CV
  for these).

Pearls additions, beyond Beads:
- **Samples:** load WAV files from the USB stick into the buffer, and save
  the buffer to the stick as a WAV file.
- **Memory:** the quality setting and a frozen buffer survive power cycles.
- **Tap tempo** on the Record button: an internal clock for the grains (or
  the delay), as if a clock were patched into the seed input.
- **Reversed grains** and **stereo spread** on the free secondary knobs.
- **Tighter gate timing** on the Freeze and Reset gates and the EOL output.

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
| **Reset** button | SEED: tap for a grain (or tap tempo in delay mode). To re-lock the grain generator, hold it for 4 s and release, or press Freeze while holding it (as on Beads) |
| **Reset** gate | SEED input: triggers or clocks grains. Seen as patched while it receives edges (4 s timeout) |
| **Next** button / gate | QUALITY, on release and in the Beads order: hi-fi (16-bit 48 kHz) → cold digital → sunny tape → scorched cassette. The encoder LEDs show the Beads colour for 1.5 s. While the buffer is frozen, the change waits until it's released (as on Beads). **Hold 1.5 s:** restart the automatic input gain (Beads: long press on the quality button) |
| **Source** button / gate | Wavetable synth mode on/off |
| **Record** button | TAP TEMPO: tap two or more times to clock the grains (or the delay) at that tempo; a single tap later re-aligns the beat; hold 1 s to stop |
| **EOL** output | Grain / delay-tap gate |
| **Reset** LED | Grain activity (dim while locked and idle) |
| **Freeze** LED | Buffer frozen (by the button, the gate, or after loading) |
| **Next** LED | Lit while pressed; slow blink while the automatic gain measures the input |
| **Record** LED | Tap tempo beat (dim between beats) |

**Secondary layer** (hold Source, as for other Nebulae instruments):

| Nebulae | Pearls |
|---|---|
| Start / Size / Window | TIME / SIZE / SHAPE randomness (fully CCW = none) |
| Pitch encoder | PITCH randomness |
| Density | Input gain: fully CCW = automatic (default), otherwise 1×–40× manual |
| Overlap | Reversed grains: fully CCW = as Beads (grains play backwards only with Size left of about 11 o'clock), clockwise = more and more of the other grains play backwards too, all of them fully clockwise |
| Blend | Stereo spread: fully CCW = as Beads, clockwise = grains spread further across the stereo field |
| Speed encoder | Nebulae input level (unchanged) |
| Freeze | Randomness distribution: off = uniform (Beads' attenurandomizer clockwise of noon), on = peaky, mostly small values with occasional extremes (counter-clockwise of noon) |
| Record | Mono buffer: left input only, twice the recording time |
| Next | Load the next WAV file from the USB stick into the buffer |
| Reset | Save the buffer to the USB stick |

With a tap tempo clock, Density works as with a clock in the Reset gate.
Clockwise of 12 o'clock it sets the chance of a grain on each beat (every
beat when fully clockwise). Counter-clockwise it divides the clock.

Reversed grains and stereo spread act on grains, not on the delay mode's
taps.

## Samples, saving and memory

**Loading a sample.** Hold Source and press Next. Pearls loads the next WAV
file from the USB stick into its buffer. It goes through the files in
alphabetical order, one per press, and wraps around after the last.
- The Nebulae copies the stick's audio files into `/home/alarm/audio` at
  boot, and Pearls reads that copy. With no files there, it reads the stick
  directly.
- Any WAV file works: 8, 16, 24 or 32-bit, or floating point, at any sample
  rate, mono or stereo. Only the first two channels of a multichannel file
  are used.
- The sample is converted to the rate of the current quality setting:
  - 48 kHz in hi-fi;
  - 32 kHz in cold digital;
  - 24 kHz in sunny tape and scorched cassette.
- The sample is cut to the buffer length:
  - about 4 s in hi-fi stereo;
  - up to about 32 s in scorched cassette mono.
- The loaded buffer is **frozen as if Freeze had been pressed**, so the
  input doesn't overwrite it. One press of Freeze releases it and recording
  starts again.
- Changing the quality, mono or synth setting clears the buffer, as on
  Beads. So, as on Beads, those changes wait while the buffer is frozen: a
  loaded sample is safe from a stray Next press. To use another quality,
  choose it before loading.

**Saving the buffer.** Hold Source and press Reset. Pearls writes the buffer
to the stick as `pearls_001.wav`, `pearls_002.wav` and so on. These are
16-bit WAV files at the buffer's own rate, mono if the buffer is mono. It
also keeps a copy in `/home/alarm/audio`, so you can load the file again
straight away. Leave the stick in while saving.

**Feedback.** While a file loads or saves, the Next LED blinks. Then the
encoder LEDs flash for 1.5 s:
- **green:** loaded;
- **blue:** saved;
- **red:** failed (no WAV files, no stick, unreadable file, or an empty
  buffer).

**Memory.**
- **Quality:** remembered, saved 2 s after the last change.
- **Frozen buffer:** as on Beads, saved after it has stayed frozen for 10 s,
  by the button, the gate, or after a load. At the next start it comes back
  frozen, as if Freeze had been pressed: one press releases it. Unfreeze it
  and the next start begins with an empty buffer.

Pearls keeps these in `/home/alarm/pearls` (`state.txt`, `buffer.wav`).
Each save rewrites a file of at most a few megabytes, which is negligible
wear for an SD card. Beads' hidden "autosave off" combo (Assign + Freeze) has
no Pearls equivalent.

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
  - Scorched cassette uses Beads' own 48 kHz + 2:1 resampling path.
  - Cold digital runs at 32 kHz through a polyphase resampler.
- **Pitch** is the Nebulae encoder (±48 semitones), without the notches of
  Beads' Pitch knob (±24 semitones).
- **Density CV** is added to the knob, so it doesn't act as 1 V/oct FM at
  audio rates as on Beads.
- **The randomness amount** is a secondary knob (0 = none) and its
  distribution the secondary Freeze button, rather than one knob either side
  of noon. Nebulae CVs are added to the knobs, so there is no CV-controlled
  randomisation.
- **Instr Tester:** its Next is an incrementing index, which can't be held.
  The first press after a pause of more than 1.5 s restarts the input gain
  instead of stepping the quality.
- **Gate timing.** Csound renders audio in bursts, so reading the gates once
  per control period put edges up to a few milliseconds early or late.
  - Since pearls5, a separate thread reads the Freeze and Reset gates every
    0.2 ms and timestamps each edge, and each edge is placed on the matching
    sample.
  - The EOL output is scheduled the same way.
  - The result is a small constant delay instead of jitter.
  - CVs still go through the Nebulae's Python control loop, so a CV that
    changes together with a seed trigger may land a few milliseconds late.

## Installing with a USB stick (no SSH)

This uses the Nebulae's normal firmware-update mechanism. The update is the
official v2.1.2 firmware plus Pearls (version `v2_1_2-pearls6`).

> **Check your hardware first.** This firmware is for Nebulae v2 units with a
> **Raspberry Pi 3** on the back. A Pi 3 has full-size HDMI and a micro-USB
> power socket; a Pi 4 has two micro-HDMI ports and USB-C. If yours has a
> Pi 4, don't install this yet.

1. Copy `neb_update.zip` to the **root of your USB stick**. Your audio files
   and instruments can stay on it. Get the file from
   `FirmwareUpdates/v_2_1_2_pearls6/` in this repository, or make it with
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
- The plugin is checked in a temporary folder first, so an unverified plugin
  never sits in Csound's plugin folder, where every instrument would load it.
- Every step has a time limit, and `startup.sh` caps the whole installer at
  3 minutes. Versions 1 and 2 froze the boot (steady white LEDs): Csound 6.11
  on the module spins forever when its output pipe is closed early, and the
  installer ran `csound --version | grep -m1`. Version 3 makes no Csound
  command-line calls.
- A copy of the log stays on the module in
  `/home/alarm/pearls/install_log.txt`.
- If the check fails, it doesn't compile on the module (no swap, 1 GB of
  RAM). Use the SSH install instead.
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
- `controlhandler.py`: `ignorealtgate` makes secondary buttons ignore their
  gate jacks. Pearls uses it so that a clock in the Reset or Next jack can't
  save or load while Source is held.
- `ui.py`: Pearls LED feedback (`ui,pearls`).
- `nebulae.py`: if an instrument's orchestra fails to compile, Nebulae falls
  back to the Granular Looper instead of failing at every boot.
- `nebulae.py`: an instrument chosen with the selector is saved as the boot
  instrument only once it has started. If Nebulae dies twice in a row
  before an instrument has run for 20 s, it starts the Granular Looper.
  (Counted in `/tmp`, which survives a service restart but not a reboot.)
- `settings.py`: fixes a crash (`UnboundLocalError: new_val`) when an
  instrument's config chunk gives a secondary (`_alt`) control a non-numeric
  value. An early `pearls.instr` did that, which crashed the module in a loop.
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
- **Next** steps the quality on each press. A press more than 1.5 s after the
  previous one restarts the input gain instead: the tester's file index
  can't be held, so Pearls can't tell a press from a hold. Put a few audio
  files in its `audio/` folder; with fewer than two, Next does nothing.
- **Reset** is a short tap, so it seeds grains and unlocks the generator.
  Holding it to re-lock isn't possible there.
- **Secondary buttons** aren't available yet. These are the mono buffer, the
  randomness flavour, loading and saving.
- **Files:** loading, saving and memory use the Nebulae's folders. On a
  computer they do nothing.
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
make test                     # engine tests (238 checks)
make bench && build/pearls_bench   # CPU use per mode; stop Nebulae first
```

- **CPU.** `pearls_bench` needs less than about 50% in the worst case (one
  core). On a desktop the worst case is 3%.
- **Audio.** Listen for clicks or dropouts at high density and in delay
  mode. If there are any, raise `-B` (then `-b`) in `pearls.instr`.
- **Controls.** Gates on Freeze and Reset, the EOL output, Next cycling through
  the four colours, synth mode on Source, tap tempo on Record.
- **Files.** Load a sample (Source + Next), save it (Source + Reset), power
  cycle with a frozen buffer.
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
python3 tools/csound_harness.py      # pearls.instr in csound, offline,
                                     # with loading, saving and a restart
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

### Simulating the Nebulae

`tools/nebulae_sim/sim.sh` runs the real Nebulae program (`Code/nebulae`,
Python 2.7) from a firmware update file on a desktop. The GPIO, SPI ADCs and
LED driver are faked, and Csound uses its null audio driver. It can boot
any instrument, switch instruments at run time as the selector does, set
controls, inject crashes, and restart Nebulae as systemd does. Run it as root
(it writes `/home/alarm`):

```sh
# Python 2.7 + NumPy 1.14 (the module's versions), e.g. with micromamba:
micromamba create -p ~/py27 -c conda-forge python=2.7 numpy=1.14
export PYTHON2=~/py27/bin/python
make                                    # desktop build of the plugin
S=tools/nebulae_sim/sim.sh FW=../../FirmwareUpdates/v_2_1_2_pearls6/neb_update.zip
$S $FW user pearls 30 build/libpearls.so                       # boot into Pearls
NEBSIM_SWITCH_TO=user/pearls $S $FW factory a_granularlooper 40 build/libpearls.so
NEBSIM_SET="source=1" NEBSIM_KEEP_LOG=1 $S $FW user pearls 20 build/libpearls.so
NEBSIM_RESTARTS=4 NEBSIM_CRASH_INSTR=pearls $S $FW user pearls 30  # boot-loop net
```

The comment at the top of `sim.sh` lists the options.

## Layout

```
pearls.instr            Nebulae instrument (control mapping, Next counter)
engine/                 host-independent engine
  pearls_engine.*         replaces beads.cc / ui.cc hardware glue
  nebulae_mapping.h       Nebulae control values -> Beads parameters
  gain_control.*          Beads AGC, adapted to float input
  rational_resampler.h    48 kHz <-> 32 kHz for vintage digital
  rpi_gpio.*              gate jacks via /dev/gpiomem
  gate_timing.h           timestamped gate edges -> sample positions
  pearls_files.*          WAV files, saved state, USB stick
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
