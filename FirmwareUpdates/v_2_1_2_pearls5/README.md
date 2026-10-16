# Nebulae v2 firmware v2_1_2-pearls5

Official firmware v2.1.2 plus **Pearls**, an unofficial port of the Mutable
Instruments Beads granular processor. See `Code/beads_port/README.md`.

New since pearls4:
- **Load samples:** hold Source and press Next to load WAV files from the
  USB stick into the buffer.
- **Save the buffer:** hold Source and press Reset to save it to the stick
  as a WAV file.
- **Memory:** the quality setting and a frozen buffer survive power cycles.
- **Tap tempo:** on the Record button.
- **New secondary knobs:** reversed grains on secondary Overlap, stereo
  spread on secondary Blend.
- **Gate timing:** tighter on the Freeze and Reset gates and the EOL output.

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
