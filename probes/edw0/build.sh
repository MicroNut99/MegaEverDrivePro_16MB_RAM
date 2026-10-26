#!/bin/bash
# edw0/build.sh - the EverDrive PRO window 0 probe (run from the EverDrive menu, 32X attached, no CD)
#   bash edw0/build.sh   -> edw0/EDW0_W.32X (CTRL0 0xA000) + edw0/EDW0_X.32X (0xE000)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -n "${SEGA_TOOLCHAIN:-}" ]; then ROOTDIR="$SEGA_TOOLCHAIN"; fi
ROOTDIR="${ROOTDIR:-/opt/toolchains/sega}"
for v in W:0xA000; do
  make -C "$HERE" clean > /dev/null
  make -C "$HERE" ROOTDIR="$ROOTDIR" ED_CTRL0="${v#*:}" > /dev/null
  cp "$HERE/edw0.32x" "$HERE/EDW0E_${v%%:*}.32X"
done
make -C "$HERE" clean > /dev/null
ls -l "$HERE"/EDW0E_*.32X
