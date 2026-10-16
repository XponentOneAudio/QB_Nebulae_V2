# Vendored upstream sources

These files are copied from Emilie Gillet's open-source Eurorack firmware.
They are MIT-licensed; the licence header in each file applies.

| Source | Repository | Commit |
|---|---|---|
| `beads/` | https://github.com/pichenettes/eurorack | `f4cfa7697095a28d292bff758beb51deb49b5a86` |
| `stmlib/` | https://github.com/pichenettes/stmlib | `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` |

Only the DSP code is included (no drivers, UI or bootloader), and only the
files the build actually uses.

## Local modifications

First change:

- `beads/dsp/processor.h`: `#define ENABLE_HARDWARE_24K` is wrapped in
  `#ifndef BEADS_NO_HARDWARE_24K`. The Nebulae codec cannot run at 24 kHz,
  so the port defines that macro, and "dirty tape" then uses the
  upstream 48 kHz + 2:1 resampling path.

Build flags `-DTEST -include cstdio` select stmlib's non-embedded code paths
and supply a missing `<cstdio>` include. No source edits are needed for them.

## Trademark note

From the upstream README: "Mutable Instruments" is a registered trademark,
and derivative works should not use that name or, ideally, the original
module name. The Nebulae instrument built from this code is therefore
called **Pearls**. It is an unofficial, non-commercial port of the Beads DSP.

Second change, also small:

- `beads/dsp/processor.{h,cc}` and `beads/dsp/synth.{h,cc}`: an optional
  `set_gate_output(bool*)` sink that receives the per-sample grain/tap gate.
  Upstream can only output the gate by replacing the right audio channel.
  Nebulae has a dedicated gate output jack (EOL), so the port keeps stereo
  audio and sends the gate there. Nothing changes when no sink is set.

`git diff --no-index` against the upstream commit shows exactly these edits.
