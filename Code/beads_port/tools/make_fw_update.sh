#!/bin/bash
# Makes the Nebulae firmware update with Pearls:
#   FirmwareUpdates/v_2_1_2_pearls1/neb_update.zip
#
# Starts from the official v2.1.2 update (FirmwareUpdates/v_2_1_2) and
# overlays only the files this branch changes or adds under Code/, with
# Windows line endings removed as the official create_fw_update.sh does
# (a shell script with CRLF line endings would break booting). Every other
# file stays byte-identical to the official firmware.
#
# Run from anywhere inside the repository, after committing: the overlay
# comes from the committed HEAD, not the working tree.

set -e
REPO=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
OFFICIAL="$REPO/FirmwareUpdates/v_2_1_2/neb_update.zip"
OUT_DIR="$REPO/FirmwareUpdates/v_2_1_2_pearls1"
BASE=$(git -C "$REPO" merge-base HEAD master)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

tar -xzf "$OFFICIAL" -C "$TMP"

git -C "$REPO" diff --name-only --diff-filter=ACMR "$BASE" HEAD -- Code |
while read -r path; do
    mkdir -p "$TMP/QB_Nebulae_V2/$(dirname "$path")"
    git -C "$REPO" show "HEAD:$path" > "$TMP/QB_Nebulae_V2/$path"
    mode=$(git -C "$REPO" ls-tree HEAD "$path" | cut -d ' ' -f 1)
    [ "$mode" = "100755" ] && chmod +x "$TMP/QB_Nebulae_V2/$path"
    # Text files only (no NUL bytes): strip carriage returns.
    if ! grep -qP '\x00' "$TMP/QB_Nebulae_V2/$path"; then
        sed -i 's/\r$//' "$TMP/QB_Nebulae_V2/$path"
    fi
    echo "  + $path"
done
git -C "$REPO" diff --name-only --diff-filter=D "$BASE" HEAD -- Code |
while read -r path; do
    rm -f "$TMP/QB_Nebulae_V2/$path"
    echo "  - $path"
done

# Sanity checks: the update must boot.
if grep -q $'\r' "$TMP/QB_Nebulae_V2/Code/scripts/startup.sh"; then
    echo "error: startup.sh has CRLF line endings"; exit 1
fi
bash -n "$TMP/QB_Nebulae_V2/Code/scripts/startup.sh"
bash -n "$TMP/QB_Nebulae_V2/Code/beads_port/boot_install.sh"
[ -d "$TMP/QB_Nebulae_V2/Code/nebulae" ]

mkdir -p "$OUT_DIR"
(cd "$TMP" && tar -czf "$OUT_DIR/neb_update.zip" QB_Nebulae_V2)
echo "Firmware update: $OUT_DIR/neb_update.zip ($(grep VERSION= "$TMP/QB_Nebulae_V2/Code/config.txt"))"
echo "source commit: $(git -C "$REPO" rev-parse --short HEAD)"
