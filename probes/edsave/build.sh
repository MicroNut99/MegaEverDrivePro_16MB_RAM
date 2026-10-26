#!/bin/bash
# edsave/build.sh - what does the EverDrive PRO keep? (run from the EverDrive menu, 32X attached, no CD)
#   bash edsave/build.sh   -> edsave/EDSAVE1.32X  (header declares save RAM: the OS should keep EDSAVE1.srm)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -n "${SEGA_TOOLCHAIN:-}" ]; then ROOTDIR="$SEGA_TOOLCHAIN"; fi
ROOTDIR="${ROOTDIR:-/opt/toolchains/sega}"
make -C "$HERE" clean > /dev/null
make -C "$HERE" ROOTDIR="$ROOTDIR" ED_SRAM=1 > /dev/null
cp "$HERE/edsave.32x" "$HERE/EDSAVE1.32X"
make -C "$HERE" clean > /dev/null
ls -l "$HERE"/EDSAVE1.32X
