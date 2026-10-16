#!/bin/bash
# Installs Pearls at boot, from the firmware itself: no SSH needed.
#
# Called by Code/scripts/startup.sh before Nebulae starts. After a firmware
# update containing a new prebuilt plugin it:
#   1. copies prebuilt/armv7/libpearls.so into the Csound plugin directory,
#   2. checks that it loads and runs in this Csound (tools/verify_plugin.py),
#      and if not, tries to compile it on the module (only if g++ is there),
#   3. installs pearls.instr into the user instrument bank,
#   4. writes pearls_install_log.txt to the USB stick, and copies pearls.instr
#      to the stick if the stick holds other .instr files (Nebulae replaces
#      the user bank with the stick's instruments).
# On every other boot it only checks a marker file and exits.
#
# It never stops Nebulae from booting: any failure is logged and skipped.
#
# The Nebulae boot scripts run as the "alarm" user and use sudo for
# privileged steps (see scripts/update.sh); so does this script.

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
INSTALLER_VERSION=2

if [ "$(id -u)" = "0" ]; then
    SUDO=""
else
    SUDO="sudo"
fi

[ -f "$PREBUILT" ] || exit 0
WANTED="prebuilt $(sha256sum "$PREBUILT" | cut -d ' ' -f 1) installer $INSTALLER_VERSION"

find_plugin_dir() {
    local d
    for d in /usr/local/lib/csound/plugins64-6.0 /usr/lib/csound/plugins64-6.0 \
             /usr/local/lib/*/csound/plugins64-6.0 /usr/lib/*/csound/plugins64-6.0; do
        if [ -d "$d" ]; then
            echo "$d"
            return
        fi
    done
    find /usr /opt -type d -name 'plugins64-6.0' 2>/dev/null | head -n 1
}
PLUGIN_DIR=$(find_plugin_dir)

# Fast path: this prebuilt is already installed (or a local build from
# install.sh, which takes precedence), and so is the instrument.
INSTALLED=$(cat "$MARKER" 2>/dev/null)
if [ -n "$PLUGIN_DIR" ] && [ -f "$PLUGIN_DIR/libpearls.so" ] &&
   { [ "$INSTALLED" = "$WANTED" ] || [ "$INSTALLED" = "local-build" ]; }; then
    if [ ! -f "$INSTR_DIR/pearls.instr" ]; then
        sh "$MOUNTFS" rw
        mkdir -p "$INSTR_DIR" && cp "$HERE/pearls.instr" "$INSTR_DIR/"
        sync
        sh "$MOUNTFS" ro
    fi
    exit 0
fi
# This prebuilt already failed here: don't retry until a new firmware.
if [ "$INSTALLED" = "failed $WANTED" ]; then
    exit 0
fi

log() {
    echo "$*"
    echo "$*" >> "$LOG"
}

verify() {
    timeout 120 python2 "$HERE/tools/verify_plugin.py" >> "$LOG" 2>&1
}

# Copies a plugin into the Csound plugin directory (owned by root).
install_plugin() {
    $SUDO cp "$1" "$PLUGIN_DIR/libpearls.so" >> "$LOG" 2>&1 &&
        $SUDO chmod 755 "$PLUGIN_DIR/libpearls.so" >> "$LOG" 2>&1 &&
        sync
}

LOG=$(mktemp /tmp/pearls_install_log.XXXXXX)
log "Pearls install - $(date)"
log "firmware: $(grep VERSION= /home/alarm/QB_Nebulae_V2/Code/config.txt 2>/dev/null | head -n 1)"
log "plugin: $(cat "$HERE/prebuilt/armv7/BUILD_INFO.txt" 2>/dev/null | grep -E 'commit|sha256' | tr '\n' ' ')"
log "csound: $(csound --version 2>&1 | grep -m1 -i 'csound version')"
log "installer: version $INSTALLER_VERSION, running as $(id -un)"

sh "$MOUNTFS" rw

RESULT=fail
if [ -z "$PLUGIN_DIR" ]; then
    log "ERROR: Csound plugin directory (plugins64-6.0) not found."
else
    log "Csound plugin directory: $PLUGIN_DIR"
    if ! install_plugin "$PREBUILT"; then
        log "ERROR: could not copy the plugin to $PLUGIN_DIR (details above)."
    fi
    if verify; then
        RESULT=ok
        mkdir -p "$STATE_DIR" && echo "$WANTED" > "$MARKER"
        log "Prebuilt plugin installed and verified."
    else
        log "The prebuilt plugin did not pass the check (details above)."
        if command -v g++ >/dev/null 2>&1; then
            log "Compiling Pearls on the module instead (this takes several minutes)..."
            rm -rf "$HERE/build"
            if timeout 1800 make -C "$HERE" -j4 >> "$LOG" 2>&1 &&
               install_plugin "$HERE/build/libpearls.so" && verify; then
                RESULT=ok
                mkdir -p "$STATE_DIR" && echo "local-build" > "$MARKER"
                log "Compiled plugin installed and verified."
            else
                log "ERROR: compiling on the module failed too."
            fi
        else
            log "No compiler on the module (g++), so it cannot be compiled here."
        fi
        if [ $RESULT != ok ]; then
            $SUDO rm -f "$PLUGIN_DIR/libpearls.so"
            # Remember the failure for this prebuilt so the next boot does
            # not retry (and slow down) until a new firmware arrives.
            mkdir -p "$STATE_DIR" && echo "failed $WANTED" > "$MARKER"
        fi
    fi
fi

if [ $RESULT = ok ]; then
    mkdir -p "$INSTR_DIR" && cp "$HERE/pearls.instr" "$INSTR_DIR/"
    log "Instrument installed: $INSTR_DIR/pearls.instr"
    log "Select it with the instrument selector (hold the Speed encoder), user bank."
else
    rm -f "$INSTR_DIR/pearls.instr"
    log "Pearls is NOT installed. Nebulae works as before."
fi

# Keep a copy of the log on the module.
mkdir -p "$STATE_DIR" && cp "$LOG" "$STATE_DIR/install_log.txt"

# Report on the USB stick.
if $SUDO mount "$USB_DEV" "$USB_DIR" 2>/dev/null || grep -q " $USB_DIR " /proc/mounts; then
    if [ $RESULT = ok ] && ls "$USB_DIR"/*.instr >/dev/null 2>&1; then
        $SUDO cp "$HERE/pearls.instr" "$USB_DIR/"
        log "Copied pearls.instr to the USB stick (it holds other instruments)."
    fi
    $SUDO cp "$LOG" "$USB_DIR/pearls_install_log.txt"
    sync
    $SUDO umount "$USB_DEV" 2>/dev/null
fi

rm -f "$LOG"
sync
sh "$MOUNTFS" ro
exit 0
