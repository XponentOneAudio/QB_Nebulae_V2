#!/bin/bash
# Installs Pearls at boot, from the firmware itself: no SSH needed.
#
# Called by Code/scripts/startup.sh (under an overall time limit) before
# Nebulae starts. After a firmware update containing a new prebuilt plugin:
#   1. checks the prebuilt plugin in a temporary plugin directory: it must
#      load and run in this module's Csound (tools/verify_plugin.py);
#   2. only then copies it into the Csound plugin directory, and installs
#      pearls.instr into the user instrument bank;
#   3. writes pearls_install_log.txt to the USB stick (and a copy to
#      /home/alarm/pearls/install_log.txt), and copies pearls.instr to the
#      stick if the stick holds other .instr files (Nebulae replaces the user
#      bank with the stick's instruments).
# A failed check is not retried until a new firmware arrives. On every other
# boot it only checks a marker file and exits.
#
# Rules that keep this script from ever blocking the boot:
#   - stdin is /dev/null for everything;
#   - every external command is time limited and force-killed (timeout -k);
#   - no `csound` command-line calls: Csound 6.10/6.11 spins forever when its
#     output pipe is closed early (e.g. `csound --version | grep -m1`), which
#     froze the boot with installer versions 1 and 2;
#   - no compiling on the module (no swap, 1 GB RAM): use install.sh over SSH.

exec < /dev/null

HERE=$(cd "$(dirname "$0")" && pwd)
PREBUILT="$HERE/prebuilt/armv7/libpearls.so"
STATE_DIR=/home/alarm/pearls
MARKER="$STATE_DIR/installed.txt"
INSTR_DIR=/home/alarm/instr
MOUNTFS=/home/alarm/QB_Nebulae_V2/Code/scripts/mountfs.sh
USB_DEV=/dev/sda1
USB_DIR=/mnt/memory
# Bump when this script changes, so that a failure recorded by an older
# version is retried.
INSTALLER_VERSION=4

if [ "$(id -u)" = "0" ]; then
    SUDO=""
else
    SUDO="sudo -n"
fi

# t SECONDS COMMAND...: run with a hard time limit.
t() {
    local secs=$1
    shift
    timeout -k 5 "$secs" "$@"
}

[ -f "$PREBUILT" ] || exit 0
WANTED="prebuilt $(t 20 sha256sum "$PREBUILT" | cut -d ' ' -f 1) installer $INSTALLER_VERSION"

find_plugin_dir() {
    local d
    for d in /usr/local/lib/csound/plugins64-6.0 /usr/lib/csound/plugins64-6.0 \
             /usr/local/lib/*/csound/plugins64-6.0 /usr/lib/*/csound/plugins64-6.0; do
        if [ -d "$d" ]; then
            echo "$d"
            return
        fi
    done
    t 60 find /usr /opt -type d -name 'plugins64-6.0' 2>/dev/null | head -n 1
}
PLUGIN_DIR=$(find_plugin_dir)

# Fast path: this prebuilt is already installed (or a local build from
# install.sh, which takes precedence), and so is this firmware's instrument.
INSTALLED=$(cat "$MARKER" 2>/dev/null)
if [ -n "$PLUGIN_DIR" ] && [ -f "$PLUGIN_DIR/libpearls.so" ] &&
   { [ "$INSTALLED" = "$WANTED" ] || [ "$INSTALLED" = "local-build" ]; }; then
    if ! cmp -s "$HERE/pearls.instr" "$INSTR_DIR/pearls.instr"; then
        t 30 sh "$MOUNTFS" rw
        mkdir -p "$INSTR_DIR" && cp "$HERE/pearls.instr" "$INSTR_DIR/"
        sync
        t 30 sh "$MOUNTFS" ro
    fi
    exit 0
fi
# This prebuilt already failed here: don't retry until a new firmware.
if [ "$INSTALLED" = "failed $WANTED" ]; then
    exit 0
fi

LOG=$(mktemp /tmp/pearls_install_log.XXXXXX)
log() {
    echo "$*"
    echo "$*" >> "$LOG"
}

log "Pearls install - $(date)"
log "firmware: $(grep VERSION= /home/alarm/QB_Nebulae_V2/Code/config.txt 2>/dev/null | head -n 1)"
log "plugin: $(grep -E 'commit|sha256' "$HERE/prebuilt/armv7/BUILD_INFO.txt" 2>/dev/null | tr '\n' ' ')"
log "installer: version $INSTALLER_VERSION, running as $(id -un)"

t 30 sh "$MOUNTFS" rw > /dev/null 2>&1

RESULT=fail
if [ -z "$PLUGIN_DIR" ]; then
    log "ERROR: Csound plugin directory (plugins64-6.0) not found."
else
    log "Csound plugin directory: $PLUGIN_DIR"
    # Check the plugin on its own first (OPCODE6DIR64 = a temporary directory
    # holding only libpearls.so; the check orchestra only needs core opcodes).
    CHECK_DIR=$(mktemp -d /tmp/pearls_check.XXXXXX)
    cp "$PREBUILT" "$CHECK_DIR/libpearls.so"
    log "Checking the plugin in this Csound..."
    t 90 python2 "$HERE/tools/verify_plugin.py" "$CHECK_DIR" >> "$LOG" 2>&1
    STATUS=$?
    rm -rf "$CHECK_DIR"
    if [ $STATUS = 0 ]; then
        if t 30 $SUDO cp "$PREBUILT" "$PLUGIN_DIR/libpearls.so" >> "$LOG" 2>&1 &&
           t 10 $SUDO chmod 644 "$PLUGIN_DIR/libpearls.so" >> "$LOG" 2>&1; then
            sync
            RESULT=ok
            mkdir -p "$STATE_DIR" && echo "$WANTED" > "$MARKER"
            log "Plugin verified and installed."
        else
            t 10 $SUDO rm -f "$PLUGIN_DIR/libpearls.so"
            log "ERROR: could not copy the plugin to $PLUGIN_DIR (details above)."
        fi
    else
        if [ $STATUS -ge 124 ]; then
            log "ERROR: the plugin check did not finish in time (status $STATUS)."
        else
            log "ERROR: the plugin check failed (status $STATUS, details above)."
        fi
        log "To build Pearls on the module instead, use install.sh over SSH."
    fi
    if [ $RESULT != ok ]; then
        # Remember the failure for this prebuilt so the next boot does not
        # retry until a new firmware arrives.
        mkdir -p "$STATE_DIR" && echo "failed $WANTED" > "$MARKER"
    fi
fi

if [ $RESULT = ok ]; then
    mkdir -p "$INSTR_DIR" && cp "$HERE/pearls.instr" "$INSTR_DIR/"
    log "Instrument installed: $INSTR_DIR/pearls.instr"
    log "Select it with the instrument selector, user bank (see README)."
else
    rm -f "$INSTR_DIR/pearls.instr"
    log "Pearls is NOT installed. Nebulae works as before."
fi

# Keep a copy of the log on the module.
mkdir -p "$STATE_DIR" && cp "$LOG" "$STATE_DIR/install_log.txt"

# Report on the USB stick.
MOUNTED_HERE=0
if grep -q " $USB_DIR " /proc/mounts; then
    USB_OK=1
elif t 20 $SUDO mount "$USB_DEV" "$USB_DIR" 2>/dev/null; then
    USB_OK=1
    MOUNTED_HERE=1
else
    USB_OK=0
fi
if [ $USB_OK = 1 ]; then
    if [ $RESULT = ok ] && ls "$USB_DIR"/*.instr >/dev/null 2>&1; then
        t 20 $SUDO cp "$HERE/pearls.instr" "$USB_DIR/"
        echo "Copied pearls.instr to the USB stick (it holds other instruments)." >> "$LOG"
    fi
    t 20 $SUDO cp "$LOG" "$USB_DIR/pearls_install_log.txt"
    sync
    if [ $MOUNTED_HERE = 1 ]; then
        t 20 $SUDO umount "$USB_DEV" 2>/dev/null
    fi
fi

rm -f "$LOG"
sync
t 30 sh "$MOUNTFS" ro > /dev/null 2>&1
exit 0
