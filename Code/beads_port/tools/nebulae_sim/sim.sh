#!/bin/bash
# Boots the Nebulae Python program from a firmware update file on a desktop,
# with fake hardware, straight into a chosen instrument, and checks that it
# keeps running.
#
#   sim.sh FIRMWARE.zip BANK INSTR [SECONDS] [PLUGIN]
#     FIRMWARE.zip  neb_update.zip (unpacked into /home/alarm, like update.sh)
#     BANK INSTR    instrument to boot, e.g. "factory a_granularlooper" or
#                   "user pearls" (written to bootinstr.txt)
#     PLUGIN        libpearls.so to put in the Csound plugin directory
#                   (desktop build; default: none)
#
# Environment:
#   NEBSIM_SWITCH_TO=bank/instr  select that instrument at run time
#   NEBSIM_SWITCH_AFTER=seconds  when (default 8)
#   NEBSIM_CRASH_INSTR=name      inject a crash when starting that instrument
#   NEBSIM_SET="name=value ..."  set control channels after NEBSIM_SET_AFTER s
#   NEBSIM_KEEP_LOG=1            keep the full log when the run passes
#   NEBSIM_RESTARTS=n            restart Nebulae up to n times when it exits
#                                (systemd Restart=on-failure); /tmp is kept
#                                between restarts, as on the module
#
# Needs: root (it writes /home/alarm), Python 2.7 with numpy as $PYTHON2
# (default python2), Csound 6 with libcsound64.so and libcsnd6.so.
# Exit status 0 when Nebulae is still running after SECONDS without a
# Python traceback.

set -u
HERE=$(cd "$(dirname "$0")" && pwd)
FIRMWARE=$1
BANK=$2
INSTR=$3
SECONDS_TO_RUN=${4:-30}
PLUGIN=${5:-}
PYTHON2=${PYTHON2:-python2}
LOG=$(mktemp /tmp/nebsim_log.XXXXXX)

PLUGIN_DIR=$(find /usr/local/lib /usr/lib -type d -name 'plugins64-6.0' 2>/dev/null | head -n 1)

rm -rf /home/alarm/QB_Nebulae_V2 /home/alarm/instr
mkdir -p /home/alarm /home/alarm/audio /home/alarm/instr /home/alarm/pd /mnt/memory
tar -xzf "$FIRMWARE" -C /home/alarm
# mountfs.sh would remount the desktop's root file system.
printf '#!/bin/bash\necho "[sim mountfs $1]"\n' > /home/alarm/QB_Nebulae_V2/Code/scripts/mountfs.sh
printf 'bank,%s\ninstr,%s\n' "$BANK" "$INSTR" > /home/alarm/QB_Nebulae_V2/Code/config/bootinstr.txt
# As boot_install.sh does: the firmware's pearls.instr goes into the user
# bank (the user bank starts empty on every run).
if [ -f /home/alarm/QB_Nebulae_V2/Code/beads_port/pearls.instr ]; then
    cp /home/alarm/QB_Nebulae_V2/Code/beads_port/pearls.instr /home/alarm/instr/
fi
rm -f "$PLUGIN_DIR/libpearls.so"
if [ -n "$PLUGIN" ]; then
    cp "$PLUGIN" "$PLUGIN_DIR/libpearls.so"
fi

# bootleds.py and friends are started with "python2" from PATH.
SHIM=$(mktemp -d /tmp/nebsim_bin.XXXXXX)
ln -s "$(command -v "$PYTHON2")" "$SHIM/python2"
export PATH="$SHIM:$PATH"
export PYTHONPATH="$HERE/fakehw"

rm -f /tmp/nebulae_start_attempts   # fresh boot
RUN=0
while true; do
    echo "NEBSIM: === Nebulae process start $RUN ===" >> "$LOG"
    timeout -k 5 "$SECONDS_TO_RUN" python2 -u "$HERE/run_sim.py" >> "$LOG" 2>&1
    STATUS=$?
    if [ $STATUS = 124 ] || [ $RUN -ge ${NEBSIM_RESTARTS:-0} ]; then
        break
    fi
    RUN=$((RUN + 1))
    # The selector / knob actions happened once, in the first run.
    unset NEBSIM_SWITCH_TO NEBSIM_SET
    echo "NEBSIM: Nebulae exited with status $STATUS, restarting (like systemd)" >> "$LOG"
done
pkill -f /home/alarm/QB_Nebulae_V2/Code/nebulae/bootleds.py 2>/dev/null
rm -rf "$SHIM"
rm -f "$PLUGIN_DIR/libpearls.so"

echo "--- nebulae.py output (NEBSIM lines, errors, last 15 lines) ---"
grep -aE "NEBSIM|Traceback|Error|falling back|Reloading|Nebulae Starting" "$LOG"
grep -av "^\[sim mountfs" "$LOG" | tail -n 15
echo "--- bootinstr.txt: $(tr '\n' ' ' < /home/alarm/QB_Nebulae_V2/Code/config/bootinstr.txt)"
echo "--- start attempts: $(cat /tmp/nebulae_start_attempts 2>/dev/null)"
echo "--- exit status $STATUS (124 = still running when stopped), restarts: $RUN"
if [ $STATUS = 124 ] && { [ -n "${NEBSIM_CRASH_INSTR:-}" ] || ! grep -q "Traceback" "$LOG"; }; then
    echo "SIM PASS: $BANK/$INSTR kept running for $SECONDS_TO_RUN s"
    if [ -n "${NEBSIM_KEEP_LOG:-}" ] || [ $RUN -gt 0 ]; then
        echo "full log: $LOG"
    else
        rm -f "$LOG"
    fi
    exit 0
fi
echo "SIM FAIL: $BANK/$INSTR (full log: $LOG)"
exit 1
