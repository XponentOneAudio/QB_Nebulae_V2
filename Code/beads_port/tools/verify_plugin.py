#!/usr/bin/env python
# Checks that the "pearls" opcode loads and runs in this Csound: compiles a
# small orchestra using it (synth mode, so it makes sound without input),
# performs one second without audio I/O and checks the output level.
#
# Used by boot_install.sh on the Nebulae (Python 2.7, the firmware's
# ctcsound.py). Also runs on a desktop with Python 3 and python3-csound.
# Exit status 0 = OK.
#
# The plugin must be in the Csound plugin directory, or pass a directory:
#   verify_plugin.py [plugin_dir]
import os
import sys

if len(sys.argv) > 1:
    os.environ["OPCODE6DIR64"] = os.path.abspath(sys.argv[1])

# The Nebulae firmware ships its own ctcsound.py next to nebulae.py.
sys.path.insert(0, "/home/alarm/QB_Nebulae_V2/Code/nebulae")
import ctcsound

ORC = """
sr = 48000
ksmps = 64
nchnls = 2
0dbfs = 1
instr 1
a0 init 0
; start size window density pitch speed overlap blend, 7 alt knobs,
; freeze reset source next mono sparse tap load save,
; igates = 0 (no GPIO access), ifiles = 0 (no files)
a1, a2, kgate, kled, kres, kq, kf, kc, kio pearls a0, a0, 0.3, 0.5, 0.5, 0.8, 0.6, 0.75, 0.2, 0.8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0
krms rms a1 + a2
chnset krms, "pearls_verify_rms"
outs a1, a2
endin
"""


def main():
    c = ctcsound.Csound()
    for option in ["-n", "-d", "-m0"]:
        c.setOption(option)
    if c.compileOrc(ORC) != 0:
        print("FAIL: orchestra using pearls did not compile (opcode not loaded)")
        return 1
    c.readScore("i1 0 1.2\n")
    c.start()
    peak = 0.0
    cycles = 0
    while c.performKsmps() == 0:
        value = c.controlChannel("pearls_verify_rms")[0]
        peak = max(peak, value)
        cycles += 1
    c.cleanup()
    c.reset()
    if cycles < 700:
        print("FAIL: performance stopped after %d k-cycles" % cycles)
        return 1
    if peak < 0.001:
        print("FAIL: no output from pearls (rms %f)" % peak)
        return 1
    print("OK: pearls loaded and running (Csound %d, output rms %.3f)" % (c.version(), peak))
    return 0


if __name__ == "__main__":
    sys.exit(main())
