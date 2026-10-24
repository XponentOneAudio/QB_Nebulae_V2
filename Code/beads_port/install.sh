#!/bin/bash
# Builds Pearls (Beads for Nebulae) on the Nebulae itself and installs it.
#
# Run on the Nebulae, logged in over SSH:
#   cd /home/alarm/QB_Nebulae_V2/Code/beads_port && ./install.sh
#
# Installs:
#   <Csound plugin dir>/libpearls.so  the "pearls" opcode. Outside the
#                                     firmware directory, so it survives
#                                     firmware updates.
#   /home/alarm/instr/pearls.instr    the instrument (user bank)
#
# Options:
#   --skip-tests        do not run the engine tests (they take a few minutes)
#   --plugin-dir DIR    Csound plugin directory, if it cannot be found
#   --uninstall         remove both files

set -e
HERE=$(cd "$(dirname "$0")" && pwd)
INSTR_DIR=/home/alarm/instr
MOUNTFS=/home/alarm/QB_Nebulae_V2/Code/scripts/mountfs.sh
RUN_TESTS=1
UNINSTALL=0
PLUGIN_DIR=${OPCODE6DIR64:-}

while [ $# -gt 0 ]; do
    case "$1" in
        --skip-tests) RUN_TESTS=0 ;;
        --plugin-dir) PLUGIN_DIR="$2"; shift ;;
        --uninstall) UNINSTALL=1 ;;
        *) echo "unknown option: $1"; exit 1 ;;
    esac
    shift
done

# Csound loads plugin libraries from this directory when Nebulae creates its
# Csound instance (an --opcode-lib option would be loaded too late, as Nebulae
# compiles the orchestra before starting Csound).
if [ -z "$PLUGIN_DIR" ]; then
    for d in /usr/local/lib/csound/plugins64-6.0 /usr/lib/csound/plugins64-6.0 \
             /usr/local/lib/*/csound/plugins64-6.0 /usr/lib/*/csound/plugins64-6.0; do
        if [ -d "$d" ]; then
            PLUGIN_DIR="$d"
            break
        fi
    done
fi
if [ -z "$PLUGIN_DIR" ]; then
    PLUGIN_DIR=$(find /usr /opt -type d -name 'plugins64-6.0' 2>/dev/null | head -n 1)
fi
if [ -z "$PLUGIN_DIR" ]; then
    echo "Could not find the Csound plugin directory (plugins64-6.0)."
    echo "Pass it with --plugin-dir DIR."
    exit 1
fi
if [ ! -d "$PLUGIN_DIR" ]; then
    echo "Csound plugin directory $PLUGIN_DIR does not exist."
    exit 1
fi
echo "Csound plugin directory: $PLUGIN_DIR"

# The root file system is normally mounted read-only.
if [ -f "$MOUNTFS" ]; then
    sh "$MOUNTFS" rw
    trap 'sync; sh "$MOUNTFS" ro' EXIT
fi

if [ $UNINSTALL = 1 ]; then
    sudo rm -f "$PLUGIN_DIR/libpearls.so"
    rm -f "$INSTR_DIR/pearls.instr" /home/alarm/pearls/installed.txt
    echo "Pearls removed."
    exit 0
fi

if ! command -v g++ >/dev/null 2>&1; then
    echo "g++ not found. Install it with: sudo pacman -S gcc make"
    exit 1
fi

# The Nebulae runs Csound in double precision (libcsound64); check anyway.
# Capture the whole output first: Csound 6.10/6.11 spins forever if its
# output pipe is closed early (e.g. by grep -q or grep -m1).
CSOUND_DOUBLE=1
if command -v csound >/dev/null 2>&1; then
    CSOUND_VERSION=$(timeout -k 5 20 csound --version < /dev/null 2>&1)
    if echo "$CSOUND_VERSION" | grep -q "float samples"; then
        CSOUND_DOUBLE=0
    fi
    echo "Csound: $(echo "$CSOUND_VERSION" | grep -m1 -i 'csound version')"
fi

echo "Building (this takes a few minutes on the Nebulae)..."
make -C "$HERE" -j4 CSOUND_DOUBLE=$CSOUND_DOUBLE

if [ $RUN_TESTS = 1 ]; then
    echo "Running engine tests..."
    make -C "$HERE" -j4 test
fi

sudo cp "$HERE/build/libpearls.so" "$PLUGIN_DIR/"
mkdir -p "$INSTR_DIR" /home/alarm/pearls
cp "$HERE/pearls.instr" "$INSTR_DIR/"
# Tells boot_install.sh not to replace this build with the prebuilt one.
echo "local-build" > /home/alarm/pearls/installed.txt

echo
echo "Installed:"
echo "  $PLUGIN_DIR/libpearls.so"
echo "  $INSTR_DIR/pearls.instr"
echo
echo "Select 'pearls' in the user bank (hold the Speed encoder for 1.5 s)."
echo "If your USB stick has .instr files in its root, also copy pearls.instr"
echo "there: Nebulae replaces /home/alarm/instr with the stick's instruments."
