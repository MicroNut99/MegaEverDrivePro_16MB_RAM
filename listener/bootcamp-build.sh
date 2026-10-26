#!/bin/bash
# CD32X BootCamp - build a bootable Sega CD + 32X disc for a game (the example: hello/)
#
#   bash build.sh                       hello -> disc/BootCamp.cue + .bin (data + CD audio)
#   GAME=mygame bash build.sh           your game folder, laid out like hello/ (see docs/02_PORTING_YOUR_GAME.md)
#   DISC_NAME=<name>                    disc/<name>.cue + .bin (default BootCamp)
#   ROOTDIR=/opt/toolchains/sega        Chilly Willy's Sega toolchain (sh-elf, m68k-elf, ldscripts, bootblocks)
#   SEGA_TOOLCHAIN=<folder>             the same, plus <folder>/usr/bin on PATH (genisoimage)
#   DIAG=1                              the diagnostic disc: the stack's boot text on (docs/09_ERROR_CODES.md)
#   REGION=US | EU | JP                 the boot block for the console's region (default US; only US is tested)
#   STREAM= CARTW= FMVRING= SLOTS= CART= the Sub-CPU's build switches (stack/subcpu/Makefile; all off by default)
#   EDCD=1                              also edboot/EDCD.32X: the disc started from a Mega EverDrive PRO, which
#                                       stays the cartridge (docs/12_CARTRIDGE_BOOT.md).  The disc still boots
#                                       the normal way too (its game ROM's 68000 jump table is the only change).
#                                       With it: M1WORDS=1 (EDCD's listener writes the cart in words), EDCD_W0=<bank>
#                                       (the EverDrive bank for the cart's first 512 KB; default 0)
# Every switch is listed in README.md, "How to build".
#
# A game folder holds:  Makefile (makes <game>.32x, a normal 32X cartridge ROM), splash.png (the boot
# picture), cd/ (data files for the disc, named as in stack/subcpu/game_files.h), music/TrackNN.wav
# (CD audio, NN = 02, 03, ... in order; optional).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
GAME="${GAME:-hello}"
G="$HERE/$GAME"
# SEGA_TOOLCHAIN=<folder>: a toolchain somewhere else (sh-elf, m68k-elf, ldscripts, bootblocks; genisoimage
# in <folder>/usr/bin if it is not installed), as in OpenJazz CD32X REV's build.sh
if [ -n "${SEGA_TOOLCHAIN:-}" ]; then ROOTDIR="$SEGA_TOOLCHAIN"; export PATH="$PATH:$SEGA_TOOLCHAIN/usr/bin"; fi
ROOTDIR="${ROOTDIR:-/opt/toolchains/sega}"
[ -x "$ROOTDIR/sh-elf/bin/sh-elf-gcc" ] || { echo "*** no toolchain at $ROOTDIR (set ROOTDIR= or SEGA_TOOLCHAIN=)"; exit 1; }
OUT="$HERE/disc"
ROMS="$HERE/stack/subcpu/ROMS"
NAME="${DISC_NAME:-BootCamp}"
[ -f "$G/Makefile" ] || { echo "*** $G/Makefile not found"; exit 1; }
command -v genisoimage >/dev/null || { echo "*** genisoimage missing (sudo apt install genisoimage)"; exit 1; }
python3 -c "import PIL" 2>/dev/null || { echo "*** Python Pillow missing (sudo apt install python3-pil)"; exit 1; }

echo "==> 0. the boot stack is unchanged"
python3 "$HERE/tools/check_boot.py"

# the layout in stack/subcpu/game_files.h: the ROM's name and the two slots (slot n = cart n x 128 KB)
GF="$HERE/stack/subcpu/game_files.h"
ROMNAME=$(sed -n 's/^#define GAME_ROM_NAME *"\(.*\)".*/\1/p' "$GF")
SLOT_D=$(sed -n 's/^#define GAME_DATA_SLOT *\([0-9]*\).*/\1/p' "$GF")
SLOT_R=$(sed -n 's/^#define GAME_RESIDENT_SLOT *\([0-9]*\).*/\1/p' "$GF")
[ -n "$ROMNAME" ] && [ -n "$SLOT_D" ] && [ -n "$SLOT_R" ] && [ "$SLOT_D" != "$SLOT_R" ] || { echo "*** game_files.h: GAME_ROM_NAME, GAME_DATA_SLOT, GAME_RESIDENT_SLOT (two different slots) needed"; exit 1; }
SAVE=$((0x3F0000)); D0=$((SLOT_D << 17)); R0=$((SLOT_R << 17))
ROM_MAX=$(( D0 < R0 ? D0 : R0 ))                                  # the ROM ends where the first slot starts
if [ "$D0" -gt "$R0" ]; then R_MAX=$((D0 - R0)); D_MAX=$((SAVE - D0)); else D_MAX=$((R0 - D0)); R_MAX=$((SAVE - R0)); fi
[ "$D_MAX" -gt 0 ] && [ "$R_MAX" -gt 0 ] || { echo "*** game_files.h: a slot is at or past the save area (0x3F0000)"; exit 1; }

echo "==> 1. the game ROM ($GAME)"
make -C "$G" clean > /dev/null 2>&1 || true     # a DIAG change must rebuild everything
make -C "$G" ROOTDIR="$ROOTDIR" DIAG="${DIAG:-0}"
ROMLIST=$(ls "$G"/*.32x 2>/dev/null || true)
[ -n "$ROMLIST" ] || { echo "*** $G made no .32x ROM"; exit 1; }
[ "$(echo "$ROMLIST" | wc -l)" = 1 ] || { echo "*** more than one .32x in $G - keep only the game's ROM there"; exit 1; }
ROM="$ROMLIST"
rm -rf "$ROMS"; mkdir -p "$ROMS"                  # stack/subcpu/ROMS is made by this script: keep nothing of yours there
if [ "${EDCD:-0}" = 1 ]; then
  python3 "$HERE/tools/prep_rom.py" --mode1 "$ROM" "$ROMS/$ROMNAME"
else
  python3 "$HERE/tools/prep_rom.py" "$ROM" "$ROMS/$ROMNAME"
fi
[ "$(stat -c %s "$ROMS/$ROMNAME")" -le "$ROM_MAX" ] || { echo "*** $ROMNAME is over $((ROM_MAX >> 10)) KB: it would overlap the first data slot (game_files.h)"; exit 1; }

echo "==> 2. the boot picture"
if [ -f "$G/splash.png" ]; then
  python3 "$HERE/tools/make_ol_splash.py" "$G/splash.png" "$ROMS/SPLASH.tmp" > /dev/null
  python3 "$HERE/tools/make_boot_image.py" "$ROMS/SPLASH.tmp" "$ROMS/IMAGE.RAW"
  rm -f "$ROMS/SPLASH.tmp"
fi

echo "==> 3. the game's data files"
if [ -d "$G/cd" ] && [ -n "$(ls -A "$G/cd")" ]; then
  for f in "$G"/cd/*; do
    n=$(stat -c %s "$f")
    # one array of names serves both the level slot (100 + n, cart 0x200000-0x3EFFFF) and the resident
    # slot (200 + n, cart 0x080000-0x1FFFFF): a bigger file would run into the next area
    BIG=$(( D_MAX > R_MAX ? D_MAX : R_MAX ))
    [ "$n" -le "$BIG" ] || { echo "*** $(basename "$f") is over $((BIG >> 10)) KB: it fits neither slot (game_files.h)"; exit 1; }
    [ "$n" -le "$D_MAX" ] || echo "    note: $(basename "$f") is over $((D_MAX >> 10)) KB: load it as 200 + n only (the resident slot)"
    [ "$n" -le "$R_MAX" ] || echo "    note: $(basename "$f") is over $((R_MAX >> 10)) KB: load it as 100 + n only (the level slot)"
  done
  cp -v "$G"/cd/* "$ROMS/"
fi

# DIAG=1: the diagnostic disc - tools/diag_switch.py swaps four boot-stack lines for their diagnostic twins
# for this build only (the Sub-CPU's green text, the Genesis display left on by the 68000 and by CDLOADER;
# docs/09_ERROR_CODES.md).  The release lines go back afterwards, even when the build fails.
if [ "${DIAG:-0}" != 1 ]; then
  python3 "$HERE/tools/diag_switch.py" release      # a release disc never ships a diagnostic line
else
  echo "==> DIAG=1: the diagnostic disc"
  trap 'python3 "$HERE/tools/diag_switch.py" off' EXIT
  python3 "$HERE/tools/diag_switch.py" on
fi

echo "==> 4. the SH2 loader (stack/sh2, unchanged)"
make -C "$HERE/stack/sh2" -f Makefile.cdloader clean > /dev/null
make -C "$HERE/stack/sh2" -f Makefile.cdloader ROOTDIR="$ROOTDIR"

echo "==> 5. the Sub-CPU program + the disc image (stack/subcpu)"
# REGION=EU / JP: the toolchain's EU_BOOT.BIN / JP_BOOT.BIN boot block for those consoles (only US is tested)
case "${REGION:-US}" in US|EU|JP) ;; *) echo "*** REGION must be US, EU or JP"; exit 1;; esac
cd "$HERE/stack/subcpu" && make clean > /dev/null && make cd ROOTDIR="$ROOTDIR" BOOTBLOCK="$ROOTDIR/bootblocks/${REGION:-US}_BOOT.BIN"
ISO="$HERE/stack/subcpu/CDROMPlayer.iso"
grep -a -q "SH2-OPENLARA-CD-OL1-LOADER" "$ISO" || { echo "*** the disc has no CDLOADER - stopping"; exit 1; }
[ -f "$HERE/stack/subcpu/cd/$ROMNAME" ] || { echo "*** $ROMNAME (GAME_ROM_NAME) is not on the disc - stopping"; exit 1; }

echo "==> 6. CD audio + the burnable image"
mkdir -p "$OUT"
TRACKS=$(ls "$G"/music/Track*.wav 2>/dev/null | sort || true)
if [ -z "$TRACKS" ]; then
  mkdir -p "$G/music"; python3 "$HERE/tools/make_test_track.py" "$G/music/Track02.wav"
  TRACKS="$G/music/Track02.wav"
fi
# a CD track must be at least 4 s (300 sectors): pad a small data track with empty sectors
python3 -c "import os,sys; p=sys.argv[1]; n=os.path.getsize(p)//2048; open(p,'ab').write(bytes(2048*(600-n))) if n<600 else None" "$ISO"
python3 "$HERE/stack/subcpu/make_mixed_cd2.py" "$ISO" $TRACKS --out "$OUT/$NAME"
echo "==> done: burn $OUT/$NAME.cue (with its .bin).  Needs a Sega CD + 32X + a 4 MB RAM cart."

# EDCD=1: the same stack's Sub-CPU program with the cartridge-boot listener (MODE1=1), built in a copy of
# stack/ (the disc keeps the normal one), then edboot/EDCD.32X around it (docs/12_CARTRIDGE_BOOT.md)
# EDCD_SAVE=1: the listener also reaches the EverDrive's save bank (cart 0x800000, MODE1-7) and the
# launcher's header declares save RAM, so the EverDrive OS keeps that bank on the SD card (bram.srm)
if [ "${EDCD:-0}" = 1 ]; then
  echo "==> 7. EDCD.32X: the disc from a Mega EverDrive PRO (cartridge boot)"
  T=$(mktemp -d)                                   # (no trap: DIAG=1 keeps its own on EXIT)
  cp -r "$HERE/stack" "$T/"
  ( cd "$T/stack/subcpu" && make clean > /dev/null && make CDROMPlayer.bin ROOTDIR="$ROOTDIR" MODE1=1 ${M1WORDS:+M1WORDS=$M1WORDS} ${M1MINVB:+M1MINVB=$M1MINVB} ${M1NOVINT:+M1NOVINT=$M1NOVINT} ${EDCD_QUIET:+QUIET=$EDCD_QUIET} ${EDCD_W0:+M1W0BANK=$EDCD_W0} M1CTRL0=$([ "${EDCD_X:-0}" = 1 ] && echo 0xC000 || echo 0x8000) ${EDCD_SAVE:+M1SAVE=$EDCD_SAVE} > "$T/mode1.log" 2>&1 ) \
    || { tail -20 "$T/mode1.log"; echo "*** the MODE1 Sub-CPU program did not build"; exit 1; }
  ROOTDIR="$ROOTDIR" bash "$HERE/edboot/build_edcd.sh" "$T/stack/subcpu/CDROMPlayer.bin" "$HERE/stack/sh2/CDLOADER.32x"
  rm -rf "$T"
  echo "==> EDCD.32X: copy edboot/EDCD.32X to the EverDrive's SD card; start it with this disc in the Sega CD"
fi
