#!/bin/bash
# edboot/build.sh - the Mega EverDrive PRO launchers for CD32X discs (docs/10_EVERDRIVE_TEST.md)
#   bash edboot/build.sh    -> edboot/EDBOOT.BIN   the launcher: the EverDrive as the RAM cart, then the CD
#                              edboot/EDBOOTD.BIN  the first diagnostic (superseded by EDPROBE)
#                              edboot/EDPROBE.BIN  the probe: what the console sees after ONE write of the C bit
#   ROOTDIR= / SEGA_TOOLCHAIN=  the toolchain, as for the main build.sh (only m68k-elf is used)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -n "${SEGA_TOOLCHAIN:-}" ]; then ROOTDIR="$SEGA_TOOLCHAIN"; fi
ROOTDIR="${ROOTDIR:-/opt/toolchains/sega}"
P="$ROOTDIR/m68k-elf/bin/m68k-elf-"
[ -x "${P}as" ] || { echo "*** no m68k-elf toolchain at $ROOTDIR (set ROOTDIR= or SEGA_TOOLCHAIN=)"; exit 1; }
B="$HERE/build"; rm -rf "$B"; mkdir -p "$B"
cd "$HERE"
# a ROM: assemble, link at 0, binary; then the header checksum
rom() {   # rom <source.s> <OUT.BIN>
  "${P}as" -m68000 --register-prefix-optional -o "$B/$1.o" "$1"
  "${P}ld" -Ttext 0 -e 0 -o "$B/$1.elf" "$B/$1.o"
  "${P}objcopy" -O binary "$B/$1.elf" "$B/$1.bin"
  python3 - "$B/$1.bin" "$HERE/$2" <<'EOF'
import sys
d = bytearray(open(sys.argv[1], 'rb').read())
assert len(d) == 0x8000, "%s must be 32 KB, it is %d bytes" % (sys.argv[2], len(d))
assert d[0x100:0x108] == b"SEGA SSF", "the header must start with SEGA SSF"
assert d[0x1F0:0x1F3] == b"JUE", "the header fields are out of place"
s = sum(int.from_bytes(d[i:i + 2], 'big') for i in range(0x200, len(d), 2)) & 0xFFFF
d[0x18E:0x190] = s.to_bytes(2, 'big')
open(sys.argv[2], 'wb').write(d)
print("%s: %d bytes, checksum %04X" % (sys.argv[2].split('/')[-1], len(d), s))
EOF
}
rom edboot.s EDBOOT.BIN
# EDBOOTD: the C program, linked at 0xFF0000, built into the ROM
"${P}gcc" -m68000 -Os -ffreestanding -fno-builtin -fomit-frame-pointer -fno-delete-null-pointer-checks --param=min-pagesize=0 -Wall -c -o "$B/edbootd.o" edbootd.c
"${P}as" -m68000 --register-prefix-optional -o "$B/edbootd_crt.o" edbootd_crt.s
"${P}gcc" -m68000 -nostdlib -T edbootd.ld -o "$B/edbootd_ram.elf" "$B/edbootd_crt.o" "$B/edbootd.o" -lgcc
"${P}objcopy" -O binary -j .text "$B/edbootd_ram.elf" "$B/edbootd_ram.bin"
SZ=$(stat -c %s "$B/edbootd_ram.bin"); [ "$SZ" -le 28672 ] || { echo "*** the RAM program is $SZ bytes (28 KB at most)"; exit 1; }
rom edbootd_rom.s EDBOOTD.BIN
# EDPROBE: the same way
"${P}gcc" -m68000 -Os -ffreestanding -fno-builtin -fomit-frame-pointer -fno-delete-null-pointer-checks --param=min-pagesize=0 -Wall -c -o "$B/edprobe.o" edprobe.c
"${P}as" -m68000 --register-prefix-optional -o "$B/edprobe_crt.o" edprobe_crt.s
"${P}gcc" -m68000 -nostdlib -T edbootd.ld -o "$B/edprobe_ram.elf" "$B/edprobe_crt.o" "$B/edprobe.o" -lgcc
"${P}objcopy" -O binary -j .text "$B/edprobe_ram.elf" "$B/edprobe_ram.bin"
SZ=$(stat -c %s "$B/edprobe_ram.bin"); [ "$SZ" -le 28672 ] || { echo "*** the RAM program is $SZ bytes (28 KB at most)"; exit 1; }
rom edprobe_rom.s EDPROBE.BIN
rm -rf "$B"
