#!/bin/bash
# Pearls recovery and diagnostics.
#
# Part of the "v2_1_2-pearls-recovery" firmware: the official v2.1.2 firmware
# plus this script. Run from scripts/startup.sh:
#   diag.sh boot    early in the boot: records the state of the module, tests
#                   Csound with and without the Pearls plugin, and removes
#                   Pearls so the module boots like the official firmware;
#   diag.sh after   two minutes after Nebulae has started: records whether
#                   it is running.
# Everything is appended to pearls_diag.txt on the USB stick, flushed after
# each step, so a partial report survives even if a step misbehaves. Every
# command is time limited and force-killed (timeout -k), so this script
# cannot block booting.

MODE=${1:-boot}
USB_DEV=/dev/sda1
USB_DIR=/mnt/memory
OUT="$USB_DIR/pearls_diag.txt"
CODE=/home/alarm/QB_Nebulae_V2/Code
MOUNTFS="$CODE/scripts/mountfs.sh"

if [ "$(id -u)" = "0" ]; then
    SUDO=""
else
    SUDO="sudo -n"
fi

USB_OK=0
MOUNTED_HERE=0
if grep -q " $USB_DIR " /proc/mounts; then
    USB_OK=1
elif timeout -k 5 20 $SUDO mount "$USB_DEV" "$USB_DIR" 2>/dev/null; then
    USB_OK=1
    MOUNTED_HERE=1
fi

say() {
    echo "$*"
    if [ $USB_OK = 1 ]; then
        echo "$*" | timeout -k 5 10 $SUDO tee -a "$OUT" > /dev/null
        sync
    fi
}

# run SECONDS COMMAND... : runs a command with a hard time limit and records
# its output (last 60 lines) and exit status. 124/137 = timed out / killed.
run() {
    local secs=$1
    shift
    say "\$ $*"
    local output
    output=$(timeout -k 5 "$secs" "$@" < /dev/null 2>&1; echo "[exit status $?]")
    say "$(echo "$output" | tail -n 61)"
    say ""
}

plugin_dirs() {
    timeout -k 5 60 find /usr /opt -type d -name 'plugins64-6.0' 2>/dev/null
}

CSOUND_TEST='
import sys
sys.path.insert(0, "/home/alarm/QB_Nebulae_V2/Code/nebulae")
import ctcsound
c = ctcsound.Csound()
c.setOption("-n")
c.setOption("-d")
r = c.compileOrc("sr=48000\nksmps=64\nnchnls=2\n0dbfs=1\ninstr 1\nendin\n")
print("Csound %d created, compileOrc result %d" % (c.version(), r))
c.cleanup()
'

if [ "$MODE" = "boot" ]; then
    say "=================================================="
    say "Pearls recovery diagnostics (boot) - $(date)"
    say "=================================================="
    say "user: $(id)"
    run 10 uname -a
    run 10 cat "$CODE/config.txt"
    run 10 uptime
    run 10 $SUDO true
    run 10 which python2 csound g++ gcc make timeout sha256sum
    run 10 python2 --version
    run 15 csound --version
    run 10 free -m
    run 10 df -h / /tmp
    say "--- Pearls state left by the previous firmware"
    run 10 ls -la /home/alarm/pearls /home/alarm/instr
    for f in /home/alarm/pearls/installed.txt /home/alarm/pearls/install_log.txt; do
        if [ -f "$f" ]; then
            say "--- $f"
            run 10 cat "$f"
        fi
    done
    run 10 ls -la /tmp

    DIRS=$(plugin_dirs)
    say "Csound plugin directories: $DIRS"
    FOUND=0
    for d in $DIRS; do
        if [ -f "$d/libpearls.so" ]; then
            FOUND=1
            run 10 ls -la "$d/libpearls.so"
            run 10 sha256sum "$d/libpearls.so"
        fi
    done

    if [ $FOUND = 1 ]; then
        say "--- Test 1: Csound with the Pearls plugin installed"
        run 60 python2 -c "$CSOUND_TEST"
        if [ -f "$CODE/beads_port/tools/verify_plugin.py" ]; then
            say "--- Test 2: Pearls load-and-run check"
            run 90 python2 "$CODE/beads_port/tools/verify_plugin.py"
        fi
    else
        say "The Pearls plugin is not installed."
    fi

    say "--- Removing Pearls"
    timeout -k 5 30 sh "$MOUNTFS" rw > /dev/null 2>&1
    for d in $DIRS; do
        if [ -f "$d/libpearls.so" ]; then
            run 20 $SUDO rm -f "$d/libpearls.so"
        fi
    done
    run 10 rm -f /home/alarm/instr/pearls.instr
    if [ -f /home/alarm/pearls/installed.txt ]; then
        run 10 mv /home/alarm/pearls/installed.txt /home/alarm/pearls/installed.before-recovery.txt
    fi
    sync
    timeout -k 5 30 sh "$MOUNTFS" ro > /dev/null 2>&1

    say "--- Test 3: Csound after removing Pearls"
    run 60 python2 -c "$CSOUND_TEST"

    say "--- Previous boots (if the journal is kept)"
    run 20 journalctl --list-boots --no-pager
    run 30 journalctl -b -1 --no-pager -n 80
    say "--- Kernel messages"
    run 20 $SUDO dmesg
    say "Boot diagnostics done - $(date). Nebulae starts next."
else
    say "--------------------------------------------------"
    say "Pearls recovery diagnostics (2 min after Nebulae started) - $(date)"
    say "--------------------------------------------------"
    run 10 uptime
    run 10 sh -c "ps aux | grep -E 'python|csound|pd ' | grep -v grep"
    run 10 free -m
    run 30 journalctl -b --no-pager -n 80
    say "After-start diagnostics done."
fi

sync
if [ $MOUNTED_HERE = 1 ]; then
    timeout -k 5 20 $SUDO umount "$USB_DEV" 2>/dev/null
fi
exit 0
