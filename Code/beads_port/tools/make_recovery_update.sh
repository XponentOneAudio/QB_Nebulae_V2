#!/bin/bash
# Makes the recovery firmware FirmwareUpdates/v_2_1_2_pearls_recovery/neb_update.zip:
# the official v2.1.2 update, unchanged, plus recovery/diag.sh, which removes
# the Pearls plugin and writes pearls_diag.txt to the USB stick (see diag.sh).
#
# Only these files differ from the official firmware:
#   Code/config.txt                  VERSION=v2_1_2-pearls-recovery
#   Code/scripts/startup.sh          official, plus two lines calling diag.sh
#   Code/beads_port/recovery/diag.sh
#   Code/beads_port/tools/verify_plugin.py

set -e
REPO=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
OFFICIAL="$REPO/FirmwareUpdates/v_2_1_2/neb_update.zip"
OUT_DIR="$REPO/FirmwareUpdates/v_2_1_2_pearls_recovery"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

tar -xzf "$OFFICIAL" -C "$TMP"
FW="$TMP/QB_Nebulae_V2/Code"

printf 'VERSION=v2_1_2-pearls-recovery\nVERSION_DATE=10/16/2026\n' > "$FW/config.txt"

mkdir -p "$FW/beads_port/recovery" "$FW/beads_port/tools"
git -C "$REPO" show HEAD:Code/beads_port/recovery/diag.sh | sed 's/\r$//' > "$FW/beads_port/recovery/diag.sh"
git -C "$REPO" show HEAD:Code/beads_port/tools/verify_plugin.py | sed 's/\r$//' > "$FW/beads_port/tools/verify_plugin.py"
chmod +x "$FW/beads_port/recovery/diag.sh"

# Official startup.sh plus the two hooks.
STARTUP="$FW/scripts/startup.sh"
sed -i 's/\r$//' "$STARTUP"
grep -q '^sh /home/alarm/QB_Nebulae_V2/Code/scripts/update.sh$' "$STARTUP"
grep -q '^echo "Running Nebulae"$' "$STARTUP"
sed -i '/^sh \/home\/alarm\/QB_Nebulae_V2\/Code\/scripts\/update.sh$/a\
\
# Pearls recovery: removes the Pearls plugin and writes pearls_diag.txt to\
# the USB stick. Time limited; never blocks booting.\
bash /home/alarm/QB_Nebulae_V2/Code/beads_port/recovery/diag.sh boot' "$STARTUP"
sed -i '/^echo "Running Nebulae"$/i\
# Pearls recovery: check that Nebulae is running two minutes from now.\
(sleep 120; bash /home/alarm/QB_Nebulae_V2/Code/beads_port/recovery/diag.sh after) > /dev/null 2>&1 &\
' "$STARTUP"
bash -n "$STARTUP"
bash -n "$FW/beads_port/recovery/diag.sh"
if grep -q $'\r' "$STARTUP"; then
    echo "error: startup.sh has CRLF line endings"; exit 1
fi

mkdir -p "$OUT_DIR"
(cd "$TMP" && tar -czf "$OUT_DIR/neb_update.zip" QB_Nebulae_V2)
echo "Recovery firmware: $OUT_DIR/neb_update.zip"
