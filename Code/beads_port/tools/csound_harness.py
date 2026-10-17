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

The opcode's files live under a temporary $PEARLS_ROOT: the run loads a WAV
file (secondary Next), saves the buffer to the "USB stick" (secondary Reset),
and lets a frozen buffer be remembered. A second run then starts with silent
input and checks that the quality setting and the frozen buffer come back.

Exit status is non-zero if Csound fails or the output is silent/invalid.
"""

import argparse
import math
import os
import re
import shutil
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
    (0.0, "gkoverlap_alt", 0.3),  # some reversed grains
    (0.0, "gkblend_alt", 0.5),    # stereo spread
    (3.0, "gkfilesel", 1),       # Next: hi-fi -> cold digital
    (3.1, "gkfilesel", 0),
    (5.0, "gkfilesel", 1),       # Next: -> sunny tape (24 kHz)
    (5.1, "gkfilesel", 0),
    (6.0, "gkfreeze", 1),
    (8.0, "gkfreeze", 0),
    (8.0, "gkloopsize", 1.0),    # delay mode
    (10.0, "gksource", 1),       # synth mode
    (12.0, "gksource", 0),
    (12.5, "gkfile_alt", 1),     # secondary Next: load sample.wav, held
    (13.5, "gkreset_alt", 1),    # secondary Reset: save to the USB stick
    (13.52, "gkreset_alt", 0),
    (14.0, "gkrecord", 1),       # tap tempo: 3 taps 0.5 s apart
    (14.05, "gkrecord", 0),
    (14.5, "gkrecord", 1),
    (14.55, "gkrecord", 0),
    (15.0, "gkrecord", 1),
    (15.05, "gkrecord", 0),
    (16.0, "gkfilesel", 1),      # Next held 2 s: restart the input gain
    (18.0, "gkfilesel", 0),
]
DURATION = 25.0                  # the frozen buffer is saved after 10 s

# Second run: a cold start with silent input.
RESTORE_TIMELINE = [t for t in TIMELINE if t[0] == 0.0]
RESTORE_DURATION = 4.0


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


def write_input(path, seconds, sr=48000, amplitude=0.4):
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sr)
        frames = bytearray()
        for i in range(int(seconds * sr)):
            t = i / sr
            l = amplitude * math.sin(2 * math.pi * 220 * t) * (0.6 + 0.4 * math.sin(2 * math.pi * 0.5 * t))
            r = amplitude * math.sin(2 * math.pi * 330 * t)
            frames += struct.pack("<hh", int(l * 32767), int(r * 32767))
        w.writeframes(bytes(frames))


def write_sample(path, seconds=1.5, sr=44100):
    """The sample to load: a 44.1 kHz mono 1 kHz tone with a decay."""
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        frames = bytearray()
        for i in range(int(seconds * sr)):
            t = i / sr
            frames += struct.pack("<h", int(0.5 * math.sin(2 * math.pi * 1000 * t) * math.exp(-t) * 32767))
        w.writeframes(bytes(frames))


def wav_info(path):
    with wave.open(path, "rb") as w:
        return w.getnchannels(), w.getframerate(), w.getnframes()


def read_output(path):
    with wave.open(path, "rb") as w:
        n, ch, width = w.getnframes(), w.getnchannels(), w.getsampwidth()
        data = w.readframes(n)
    assert width == 2, "expected 16-bit output"
    samples = struct.unpack("<%dh" % (n * ch), data)
    return [s / 32768.0 for s in samples], ch, 48000


def run_csound(args, config, instr, root, timeline, duration, amplitude, tag):
    tmp = os.path.join(root, "run_" + tag)
    os.makedirs(tmp)
    out = args.out if (args.out and tag == "main") else os.path.join(tmp, "out.wav")
    inp = os.path.join(tmp, "in.wav")
    write_input(inp, duration + 1, amplitude=amplitude)

    preamble = "ksmps = %s\nsr = %s\n" % (config.get("ksmps", ["128"])[0], config.get("sr", ["48000"])[0])
    # One tiny instrument per timeline event, started from the score, so the
    # driver needs no conditionals (if/then crashes the armhf Csound 6.10 of
    # Ubuntu 18.04 under qemu).
    if args.next_style == "increment":
        # Instr Tester: Next increments the file index instead of pulsing.
        original, timeline, index = timeline, [], 0
        for t, chn, value in original:
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
    # Report the opcode's status twice a second.
    driver.append('\ninstr 98\nkq chnget "pearls_quality"\nkf chnget "pearls_frozen"\n'
                  'kc chnget "pearls_clock"\nki chnget "pearls_io"\n'
                  'printks "PEARLS_STATUS %d %d %.1f %d\\n", 0.5, kq, kf, kc, ki\nendin\n')
    header = nebulae_orc_header(args.conductor)
    if args.no_chnexport:
        header = re.sub(r"(gk\w+)\s+chnexport\s+\"\w+\",\s*\d", r"\1 init 0", header)
    orc = preamble + header + "\nginumfiles init 0\n" + instr + "".join(driver)
    sco = "".join(events) + "i1 0.01 %f\ni98 0.01 %f\n" % (duration, duration)

    with open(os.path.join(tmp, "test.orc"), "w") as f:
        f.write(orc)
    with open(os.path.join(tmp, "test.sco"), "w") as f:
        f.write(sco)

    cmd = args.csound.split() + ["-d", "-m0", "-W", "-o", out, "-i", inp, "--nchnls_i=2"]
    env = dict(os.environ)
    env["PEARLS_ROOT"] = root
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
        return None, None, log
    samples, ch, sr = read_output(out)
    status = [tuple(float(v) for v in m) for m in
              re.findall(r"PEARLS_STATUS (\S+) (\S+) (\S+) (\S+)", log)]
    for line in log.splitlines():
        if line.startswith("pearls:"):
            print("  " + line)
    return (samples, ch, sr), status, out


def rms(audio, t0, t1):
    samples, ch, sr = audio
    a, b = int(t0 * sr) * ch, int(t1 * sr) * ch
    seg = samples[a:b]
    return math.sqrt(sum(s * s for s in seg) / max(1, len(seg)))


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
    root = tempfile.mkdtemp(prefix="pearls_")
    print("PEARLS_ROOT=" + root)
    os.makedirs(os.path.join(root, "home/alarm/audio"))
    os.makedirs(os.path.join(root, "mnt/memory"))
    write_sample(os.path.join(root, "home/alarm/audio/sample.wav"))

    audio, status, out = run_csound(args, config, instr, root, TIMELINE, DURATION, 0.4, "main")
    if audio is None:
        return 1
    ok = True
    def check(cond, message):
        nonlocal ok
        if not cond:
            ok = False
            print("  FAIL: " + message)

    # Next at 3 s and 5 s: hi-fi (3) -> cold digital (2) -> sunny tape (1).
    # The Instr Tester's index can't tell a press from a hold: its first
    # press (held until the next one) restarts the gain instead, and the
    # "long press" at 16 s is one more press there.
    increment = args.next_style == "increment"
    qualities = [int(q[0]) for q in status]
    steps = [q for i, q in enumerate(qualities) if i == 0 or q != qualities[i - 1]]
    print("  quality sequence: %s" % steps)
    want_steps = [3, 2, 1]
    check(steps == want_steps, "Next should step quality %s" % want_steps)
    final_quality = 1
    for label, t0, t1 in [("digital granular", 1.5, 3.0), ("dirty tape", 3.5, 5.0),
                          ("clean tape", 5.5, 6.0), ("frozen", 6.5, 8.0),
                          ("delay mode", 9.0, 10.0), ("synth", 10.5, 12.0),
                          ("loaded sample", 13.0, 14.0), ("tap tempo", 16.0, 18.0)]:
        level = rms(audio, t0, t1)
        print("  %-18s rms %.3f" % (label, level))
        check(level >= 0.01, "%s is silent" % label)

    def status_at(t):
        return status[min(int(t * 2), len(status) - 1)]
    print("  status at 7 s %s, 13 s %s, 14 s %s, 17 s %s" % (
        status_at(7.0), status_at(13.0), status_at(14.0), status_at(17.0)))
    check(status_at(7.0)[1] == 1, "frozen at 7 s")
    check(status_at(9.0)[1] == 0, "not frozen at 9 s")
    check(any(st[3] == 2 for st in status[25:30]), "load reported (io = 2)")
    check(status_at(13.0)[1] == 1, "held frozen after the load")
    check(any(st[3] == 3 for st in status[27:32]), "save reported (io = 3)")
    check(status_at(17.0)[2] > 0, "tap tempo clock running")
    if not increment:
        check(any(st[3] == 5 for st in status[36:46]),
              "Next held 1.5 s restarts the input gain (io = 5)")
    saved = os.path.join(root, "mnt/memory/pearls_001.wav")
    check(os.path.exists(saved), "pearls_001.wav on the USB stick")
    check(os.path.exists(os.path.join(root, "home/alarm/audio/pearls_001.wav")),
          "pearls_001.wav in the audio folder")
    if os.path.exists(saved):
        info = wav_info(saved)
        print("  saved WAV: %d channel(s), %d Hz, %d frames" % info)
        # The 1.5 s sample at the buffer rate: sunny tape 24 kHz, or cold
        # digital 32 kHz (Instr Tester sequence).
        rate = 32000 if increment else 24000
        check(info[1] == rate and abs(info[2] - 1.5 * rate) < 10,
              "saved WAV should hold the 1.5 s sample at %d Hz" % rate)
    state_file = os.path.join(root, "home/alarm/pearls/state.txt")
    state = open(state_file).read().split() if os.path.exists(state_file) else []
    print("  state.txt: %s" % " ".join(state))
    check("quality" in state and state[state.index("quality") + 1] == str(final_quality),
          "quality %d remembered" % final_quality)
    check("frozen" in state and state[state.index("frozen") + 1] == "1",
          "frozen buffer remembered")
    check(os.path.exists(os.path.join(root, "home/alarm/pearls/buffer.wav")),
          "buffer.wav saved")

    # Power cycle: silent input, nothing touched.
    audio2, status2, _ = run_csound(args, config, instr, root, RESTORE_TIMELINE,
                                    RESTORE_DURATION, 0.0, "restore")
    if audio2 is None:
        return 1
    level = rms(audio2, 2.0, 4.0)
    print("  after restart: status %s, rms %.3f (silent input)" % (status2[-1], level))
    check(status2 and status2[0][0] == final_quality, "quality restored")
    check(status2 and status2[-1][1] == 1, "buffer restored frozen")
    check(level >= 0.01, "restored buffer plays")

    print("output: " + out)
    print("PASS" if ok else "FAIL")
    if ok:
        shutil.rmtree(root)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
