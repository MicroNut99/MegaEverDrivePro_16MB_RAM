#!/bin/bash
# edsave/build.sh - what does the EverDrive PRO keep? (run from the EverDrive menu, 32X attached, no CD)
#   bash edsave/build.sh   -> edsave/EDSAVE3.32X  (header declares save RAM: the OS should keep EDSAVE3.srm)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -n "${SEGA_TOOLCHAIN:-}" ]; then ROOTDIR="$SEGA_TOOLCHAIN"; fi
ROOTDIR="${ROOTDIR:-/opt/toolchains/sega}"
make -C "$HERE" clean > /dev/null
make -C "$HERE" ROOTDIR="$ROOTDIR" ED_SRAM=1 > /dev/null
cp "$HERE/edsave3.32x" "$HERE/EDSAVE3.32X"
make -C "$HERE" clean > /dev/null
ls -l "$HERE"/EDSAVE3.32X
