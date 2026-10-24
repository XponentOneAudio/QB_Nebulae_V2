# Nebulae v2 firmware v2_1_2-pearls7

Official firmware v2.1.2 plus **Pearls**, an unofficial port of the Mutable
Instruments Beads granular processor. See `Code/beads_port/README.md`.

New since pearls6: **pitch quantisation.** Hold Next and press Record to
step through the scales: off, chromatic, major, minor, major pentatonic, minor
pentatonic, octaves and fifths, octaves. The Pitch encoder LEDs show the
scale's colour for 1.5 s (white, red, amber, yellow, green, cyan, blue,
magenta). Each grain's pitch, random variation included, snaps to the scale;
the root is the recorded pitch. The scale is remembered.

pearls6 (closer to the Beads manual):
- **Seed re-lock:** hold Reset for 4 s and release, or press Freeze while
  holding Reset.
- **Frozen buffer protected:** quality, mono and synth changes wait while the
  buffer is frozen.
- **Loaded or restored buffers:** frozen as if Freeze were pressed; one press
  releases them.
- **Frozen buffer saved:** after 10 s frozen, by the button or the gate.
- **Quality:** steps on release of Next, downwards as on Beads (hi-fi → cold
  digital → sunny tape → scorched cassette).
- **Input gain:** hold Next for 1.5 s to restart the automatic gain, on release
  (the Next LED blinks slowly while it measures).

pearls5 added samples from USB, saving, memory, tap tempo, reversed grains,
stereo spread and tighter gate timing.

For Nebulae v2 units with a Raspberry Pi 3 on the back.

1. Copy `neb_update.zip` to the root of the USB stick.
2. Boot the Nebulae with the stick in. It updates and reboots.
3. Pearls installs itself on the next boot and writes
   `pearls_install_log.txt` to the stick.
4. Choose `pearls` in the user bank of the instrument selector (hold the
   Speed encoder for 1.5 s).

To go back to pearls4 (tested on hardware), use
`../v_2_1_2_pearls4/neb_update.zip` the same way. For the official firmware,
use `../v_2_1_2/neb_update.zip`.

Built with `Code/beads_port/tools/make_fw_update.sh`: the official v2.1.2
update with this branch's changed files overlaid.
