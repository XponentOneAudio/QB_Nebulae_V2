# Nebulae v2 firmware v2_1_2-pearls4

Official firmware v2.1.2 plus **Pearls**, an unofficial port of the Mutable
Instruments Beads granular processor. See `Code/beads_port/README.md`.

For Nebulae v2 units with a Raspberry Pi 3 on the back.

1. Copy `neb_update.zip` to the root of the USB stick.
2. Boot the Nebulae with the stick in. It updates and reboots.
3. Pearls installs itself on the next boot and writes
   `pearls_install_log.txt` to the stick.
4. Choose `pearls` in the user bank of the instrument selector (hold the
   Speed encoder for 1.5 s).

To go back, use `../v_2_1_2/neb_update.zip` the same way.

Built with `Code/beads_port/tools/make_fw_update.sh`: the official v2.1.2
update with this branch's changed files overlaid.
