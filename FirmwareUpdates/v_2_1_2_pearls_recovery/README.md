# Nebulae v2 recovery firmware v2_1_2-pearls-recovery

The official v2.1.2 firmware, unchanged, plus a diagnostic step
(`Code/beads_port/recovery/diag.sh`) that:
- removes the Pearls Csound plugin and instrument, so the module boots like
  the official firmware;
- writes `pearls_diag.txt` to the USB stick, describing the module (Csound,
  Python, users, Pearls state) and testing Csound with and without the plugin;
- two minutes after Nebulae starts, records whether it is running.

Install it like any update: put `neb_update.zip` on the root of the stick and
boot with the stick in. Keep the stick in for the next boot as well.
