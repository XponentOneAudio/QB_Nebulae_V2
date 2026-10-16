#!/usr/bin/env python3
"""Offline test of a Nebulae .instr file with the Pearls opcode (desktop).

Builds the orchestra the same way nebulae/conductor.py does (same header with
the chnexport'd control channels, same nebconfig handling), adds a driver
instrument that plays a control timeline instead of the Nebulae hardware, and
renders it with the csound command-line program.

    tools/csound_harness.py [--instr PATH] [--lib PATH] [--out out.wav]
                            [--csound CMD] [--plugin-dir DIR]

--plugin-dir loads libpearls.so from a plugin directory (OPCODE6DIR64), as on
the Nebulae, instead of --opcode-lib. --csound runs another csound binary,
e.g. an ARM one under emulation:
    --csound "qemu-arm -L /path/to/armhf-root /path/to/armhf-root/usr/bin/csound"

Exit status is non-zero if Csound fails or the output is silent/invalid.
"""

import argparse
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
CODE = os.path.dirname(PORT)

# Control timeline: (time in seconds, channel, value). Values are Nebulae
# channel values, as the Python control layer would send them.
TIMELINE = [
    (0.0, "gkspeed", 0.75),      # reverb 0.5
    (0.0, "gkpitch", 0.6),       # unison
    (0.0, "gkloopstart", 0.3),
    (0.0, "gkloopsize", 0.5),
    (0.0, "gkwindow", 0.5),
    (0.0, "gkdensity", 0.8),
    (0.0, "gkoverlap", 0.3),
    (0.0, "gkblend", 0.8),
    (0.0, "gkloopstart_alt", 0.4),
    (0.0, "gkpitch_alt", 0.3),
    (3.0, "gkfilesel", 1),       # Next: digital -> dirty tape
    (3.02, "gkfilesel", 0),
    (5.0, "gkfilesel", 1),       # Next: -> clean tape
    (5.02, "gkfilesel", 0),
    (6.0, "gkfreeze", 1),
    (8.0, "gkfreeze", 0),
    (8.0, "gkloopsize", 1.0),    # delay mode
    (10.0, "gksource", 1),       # synth mode
    (12.0, "gksource", 0),
]
DURATION = 14.0


def parse_instr(path):
    config, orc, inside = {}, [], False
    with open(path) as f:
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
    return config, "".join(orc)


def nebulae_orc_header(conductor):
    with open(conductor) as f:
        source = f.read()
    match = re.search(r'self\.orcheader = """(.*?)"""', source, re.S)
    return match.group(1)


def write_input(path, seconds, sr=48000):
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sr)
        frames = bytearray()
        for i in range(int(seconds * sr)):
            t = i / sr
            l = 0.4 * math.sin(2 * math.pi * 220 * t) * (0.6 + 0.4 * math.sin(2 * math.pi * 0.5 * t))
            r = 0.4 * math.sin(2 * math.pi * 330 * t)
            frames += struct.pack("<hh", int(l * 32767), int(r * 32767))
        w.writeframes(bytes(frames))


def read_output(path):
    with wave.open(path, "rb") as w:
        n, ch, width = w.getnframes(), w.getnchannels(), w.getsampwidth()
        data = w.readframes(n)
    assert width == 2, "expected 16-bit output"
    samples = struct.unpack("<%dh" % (n * ch), data)
    return [s / 32768.0 for s in samples], ch, 48000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--instr", default=os.path.join(PORT, "pearls.instr"))
    parser.add_argument("--lib", default=os.path.join(PORT, "build", "libpearls.so"))
    parser.add_argument("--out", default=None)
    parser.add_argument("--csound", default="csound")
    parser.add_argument("--plugin-dir", default=None)
    parser.add_argument("--conductor", default=os.path.join(CODE, "nebulae", "conductor.py"),
                        help="conductor.py to take the orchestra header from, e.g. the "
                             "Qu-Bit Instr Tester's instr_tester/conductor.py")
    parser.add_argument("--next-style", choices=["pulse", "increment"], default="pulse",
                        help="how Next changes gkfilesel: 0/1 pulses (Nebulae) or an "
                             "incrementing file index (Instr Tester)")
    parser.add_argument("--no-chnexport", action="store_true",
                        help="declare the control globals with init instead of chnexport "
                             "(Ubuntu 18.04's armhf Csound 6.10 crashes on chnexport under qemu)")
    args = parser.parse_args()

    config, instr = parse_instr(args.instr)
    tmp = tempfile.mkdtemp(prefix="pearls_")
    out = args.out or os.path.join(tmp, "out.wav")
    inp = os.path.join(tmp, "in.wav")
    write_input(inp, DURATION + 1)

    preamble = "ksmps = %s\nsr = %s\n" % (config.get("ksmps", ["128"])[0], config.get("sr", ["48000"])[0])
    # One tiny instrument per timeline event, started from the score, so the
    # driver needs no conditionals (if/then crashes the armhf Csound 6.10 of
    # Ubuntu 18.04 under qemu).
    timeline = TIMELINE
    if args.next_style == "increment":
        # Instr Tester: Next increments the file index instead of pulsing.
        timeline, index = [], 0
        for t, chn, value in TIMELINE:
            if chn == "gkfilesel":
                if value == 0:
                    continue
                index += 1
                value = index
            timeline.append((t, chn, value))
    driver, events = [], []
    for n, (t, chn, value) in enumerate(timeline):
        driver.append("\ninstr %d\n%s init %s\nendin\n" % (100 + n, chn, value))
        events.append("i%d %f 0.001\n" % (100 + n, t))
    # Report the quality setting twice a second, to check that Next steps it.
    driver.append('\ninstr 98\nprintks "PEARLS_QUALITY %d\\n", 0.5, gkpearls_quality\nendin\n')
    header = nebulae_orc_header(args.conductor)
    if args.no_chnexport:
        header = re.sub(r"(gk\w+)\s+chnexport\s+\"\w+\",\s*\d", r"\1 init 0", header)
    orc = preamble + header + "\nginumfiles init 0\n" + instr + "".join(driver)
    sco = "".join(events) + "i1 0.01 %f\ni98 0.01 %f\n" % (DURATION, DURATION)

    with open(os.path.join(tmp, "test.orc"), "w") as f:
        f.write(orc)
    with open(os.path.join(tmp, "test.sco"), "w") as f:
        f.write(sco)

    cmd = args.csound.split() + ["-d", "-m0", "-W", "-o", out, "-i", inp, "--nchnls_i=2"]
    env = dict(os.environ)
    if args.plugin_dir:
        env["OPCODE6DIR64"] = os.path.abspath(args.plugin_dir)
    else:
        cmd.append("--opcode-lib=" + args.lib)
    for key in ("-b", "-B"):
        if key in config:
            cmd.append(key + config[key][0])
    cmd += [os.path.join(tmp, "test.orc"), os.path.join(tmp, "test.sco")]
    print(" ".join(cmd))
    result = subprocess.run(cmd, capture_output=True, text=True, env=env)
    log = result.stdout + result.stderr
    if result.returncode != 0 or "error" in log.lower() and "0 errors" not in log.lower():
        print(log[-4000:])
        print("FAIL: Csound returned %d" % result.returncode)
        return 1

    samples, ch, sr = read_output(out)
    ok = True
    # Next at 3 s and 5 s: digital (3) -> dirty tape (0) -> clean tape (1).
    qualities = [int(q) for q in re.findall(r"PEARLS_QUALITY (\d+)", log)]
    steps = [q for i, q in enumerate(qualities) if i == 0 or q != qualities[i - 1]]
    print("  quality sequence: %s" % steps)
    if steps != [3, 0, 1]:
        ok = False
        print("  FAIL: Next should step quality 3 -> 0 -> 1")
    def rms(t0, t1):
        a, b = int(t0 * sr) * ch, int(t1 * sr) * ch
        seg = samples[a:b]
        return math.sqrt(sum(s * s for s in seg) / max(1, len(seg)))
    for label, t0, t1 in [("digital granular", 1.5, 3.0), ("dirty tape", 3.5, 5.0),
                          ("clean tape", 5.5, 6.0), ("frozen", 6.5, 8.0),
                          ("delay mode", 9.0, 10.0), ("synth", 10.5, 12.0),
                          ("back to audio", 13.0, 14.0)]:
        level = rms(t0, t1)
        print("  %-18s rms %.3f" % (label, level))
        if level < 0.01:
            ok = False
            print("  FAIL: %s is silent" % label)
    print("output: " + out)
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
