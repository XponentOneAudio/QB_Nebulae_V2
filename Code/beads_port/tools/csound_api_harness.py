#!/usr/bin/env python3
"""Runs pearls.instr through the Csound API the way the Nebulae firmware does.

Mirrors nebulae/nebulae.py + conductor.py + control.py: the plugin is loaded
from the plugin directory when the Csound instance is created, options are
set, the orchestra is compiled *before* csound.start(), and control values
are written to channel pointers between k-cycles. Audio goes to files
instead of the codec.

    tools/csound_api_harness.py [--lib-dir build]

Needs the ctcsound Python module (e.g. Debian/Ubuntu package python3-csound).
"""

import argparse
import math
import os
import re
import struct
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
CODE = os.path.dirname(PORT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lib-dir", default=os.path.join(PORT, "build"),
                        help="directory holding libpearls.so")
    parser.add_argument("--instr", default=os.path.join(PORT, "pearls.instr"))
    args = parser.parse_args()

    # Stand-in for installing into the Csound plugin directory. The orchestra
    # only uses core opcodes besides "pearls", so replacing the plugin
    # directory is fine here. Must be set before the instance is created.
    os.environ["OPCODE6DIR64"] = os.path.abspath(args.lib_dir)
    import ctcsound

    config, orc, inside = {}, [], False
    with open(args.instr) as f:
        for line in f:
            if line.startswith("nebconfigbegin"):
                inside = True
            elif inside and line.startswith("nebconfigend"):
                inside = False
            elif inside:
                fields = line.rstrip().split(",")
                config[fields[0]] = fields[1:]
            else:
                orc.append(line)
    with open(os.path.join(CODE, "nebulae", "conductor.py")) as f:
        header = re.search(r'self\.orcheader = """(.*?)"""', f.read(), re.S).group(1)
    preamble = "ksmps = %s\nsr = %s\n" % (config["ksmps"][0], config["sr"][0])
    cur_orc = preamble + header + "\nginumfiles init 0\n" + "".join(orc)

    tmp = tempfile.mkdtemp(prefix="pearls_api_")
    inp, out = os.path.join(tmp, "in.wav"), os.path.join(tmp, "out.wav")
    with wave.open(inp, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(48000)
        w.writeframes(b"".join(
            struct.pack("<hh", int(12000 * math.sin(2 * math.pi * 220 * i / 48000)),
                        int(12000 * math.sin(2 * math.pi * 330 * i / 48000)))
            for i in range(48000 * 8)))

    # The opcode's files (saved state) go to a temporary directory.
    os.environ["PEARLS_ROOT"] = tmp
    c = ctcsound.Csound()
    c.setOption("-i" + inp)      # nebulae.py: -iadc:hw:0,0
    c.setOption("-o" + out)      # nebulae.py: -odac:hw:0,0
    c.setOption("-B" + config["-B"][0])
    c.setOption("-b" + config["-b"][0])
    c.setOption("-m0")
    c.setOption("-d")
    if c.compileOrc(cur_orc) != 0:
        print("FAIL: orchestra did not compile (is libpearls.so in %s?)" % args.lib_dir)
        return 1
    c.readScore("f 0 2147483641\ni1 1 -10\n")   # as conductor.py
    c.start()

    def channel(name, output=False):
        kind = ctcsound.CSOUND_OUTPUT_CHANNEL if output else ctcsound.CSOUND_INPUT_CHANNEL
        ptr, _ = c.channelPtr(name, ctcsound.CSOUND_CONTROL_CHANNEL | kind)
        return ptr

    for name, value in [("speed", 0.75), ("pitch", 0.8), ("start", 0.2), ("size", 0.5),
                        ("window", 0.4), ("density", 0.85), ("overlap", 0.2), ("blend", 0.9)]:
        channel(name)[0] = value
    next_button = channel("file")
    quality = channel("pearls_quality", output=True)
    seedled = channel("pearls_seedled", output=True)

    kr = 48000 // int(config["ksmps"][0])
    qualities, seed_leds = [], []
    for k in range(7 * kr):
        next_button[0] = 1 if 4 * kr <= k < 4 * kr + 8 else 0   # Next at 4 s
        if c.performKsmps() != 0:
            break
        qualities.append(quality[0])
        seed_leds.append(seedled[0])
    c.cleanup()
    c.reset()

    with wave.open(out, "rb") as w:
        n, ch = w.getnframes(), w.getnchannels()
        data = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    tail = data[3 * 48000 * ch:]
    rms = math.sqrt(sum((s / 32768.0) ** 2 for s in tail) / max(1, len(tail)))

    ok = True
    print("  output rms after 3 s: %.3f" % rms)
    print("  quality before / after Next: %d / %d" % (qualities[3 * kr], qualities[-1]))
    print("  seed LED activity seen: %s" % (max(seed_leds) >= 1.0))
    if rms < 0.01:
        ok = False
        print("FAIL: silent output")
    if qualities[3 * kr] != 3 or qualities[-1] != 2:
        ok = False
        print("FAIL: Next should step quality from hi-fi (3) to cold digital (2)")
    if max(seed_leds) < 1.0:
        ok = False
        print("FAIL: no grain activity reported")
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
