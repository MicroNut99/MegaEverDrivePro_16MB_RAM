#!/bin/bash
# edboot/build_edcd.sh - EDCD.32X, a CD32X disc started from a Mega EverDrive PRO (docs/12_CARTRIDGE_BOOT.md)
#   bash edboot/build_edcd.sh <the stack's Sub-CPU program built with MODE1=1> <stack/sh2/CDLOADER.32x> [<out>]
# build.sh runs it with EDCD=1 (so do other CD32X games' builds: OpenLara CD32X REV); <out> defaults to
# edboot/EDCD.32X.  It needs nothing else.  ROOTDIR= / SEGA_TOOLCHAIN= as for build.sh.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
[ $# = 2 ] || [ $# = 3 ] || { echo "usage: bash edboot/build_edcd.sh <app_mode1.bin> <CDLOADER.32x> [<out.32X>]"; exit 1; }
APP="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
CDL="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
OUT="$HERE/EDCD.32X"
if [ $# = 3 ]; then OUT="$(cd "$(dirname "$3")" && pwd)/$(basename "$3")"; fi
if [ -n "${SEGA_TOOLCHAIN:-}" ]; then ROOTDIR="$SEGA_TOOLCHAIN"; fi
ROOTDIR="${ROOTDIR:-/opt/toolchains/sega}"
P="$ROOTDIR/m68k-elf/bin/m68k-elf-"
[ -x "${P}as" ] || { echo "*** no m68k-elf toolchain at $ROOTDIR"; exit 1; }
B="$HERE/build"; rm -rf "$B"; mkdir -p "$B"
cd "$HERE"
cp "$APP" "$B/app_mode1.bin"
# CDLOADER's code: its CD boot table (0x38 bytes) off; the table's length is checked by make_edcd.py
python3 -c "import sys; d=open(sys.argv[1],'rb').read(); open(sys.argv[2],'wb').write(d[0x38:])" "$CDL" "$B/cdloader_code.bin"
AS="${P}as -m68000 --register-prefix-optional"
$AS -o "$B/edcd_sp.o" edcd_sp.s
"${P}ld" -Ttext 0x6000 -e 0x6000 -o "$B/edcd_sp.elf" "$B/edcd_sp.o"
"${P}objcopy" -O binary "$B/edcd_sp.elf" "$B/edcd_sp.bin"
# EDCD_W0=<bank>: the EverDrive bank in window 0 after the hand-over (edcd_md.s ED_W0; default 0)
# EDCD_X=1: CTRL0 = 0xE000 (P + X + W, the EverDrive's 32X mode) instead of 0xA000; both edtest-proven
# MODE1-6: W (bit 13) stays OFF: the listener turns it on only inside a copy into the cart (hw_md.s)
CTRL0=0x8000; [ "${EDCD_X:-0}" = 1 ] && CTRL0=0xC000
$AS --defsym ED_W0=${EDCD_W0:-0} --defsym ED_CTRL0=$CTRL0 -o "$B/edcd_md.o" edcd_md.s
$AS -o "$B/kos.o" ../stack/subcpu/kos.s
"${P}ld" -Ttext 0x880800 -e _start -o "$B/edcd_md.elf" "$B/edcd_md.o" "$B/kos.o"
"${P}objcopy" -O binary "$B/edcd_md.elf" "$B/edcd_md.bin"
"${P}nm" "$B/edcd_md.elf" > "$B/edcd_md.sym"
python3 make_edcd.py "$B/edcd_md.bin" "$B/edcd_md.sym" "$CDL" ../edtest/src/crt0.s "$OUT"
rm -rf "$B"
