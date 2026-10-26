#!/usr/bin/env python3
"""make_edcd.py - put EDCD.32X together (docs/12_CARTRIDGE_BOOT.md; edboot/build_edcd.sh runs it)

    python3 make_edcd.py <launcher.bin> <launcher.elf syms> <CDLOADER.32x> <edtest/src/crt0.s> <EDCD.32X>

A 32X cartridge ROM for the Mega EverDrive PRO:
  0x000  68000 vectors (all to the 32X start-up at 0x3F0, as every 32X cartridge)
  0x100  header "SEGA SSF": the EverDrive turns its extended SSF mapper on
  0x200  the 68000 jump table the 32X vector ROM sends the vectors to: reset -> the launcher (0x880800),
         every other vector -> the RAM jump table a CD boot's BIOS leaves (VECTOR_RAM; the launcher
         builds it at 0xFFFD00, the stack's listener hooks its vblank there)
  0x3C0  the 32X header: the boot ROMs load the stack's SH2 loader (CDLOADER, from its CD boot table)
  0x3F0  Sega's 32X start-up and security code (as in edtest/src/crt0.s, console-proven with the EverDrive)
  0x800  the launcher (edcd_md.s, linked at 0x880800) with the Sub-CPU programs and CDLOADER's code
"""
import re
import struct
import os, sys

# vector n (2..47) -> where the Sega CD BIOS's vector table sends it (the same in the US, EU and JP
# BIOSes).  None = the BIOS's ROM (0x000200): those go to the extra entry 0xFFFDA8.
VECTOR_RAM = {3: 0xFFFD7E, 4: 0xFFFD7E, 5: 0xFFFD84, 6: 0xFFFD78, 7: 0xFFFD8A, 8: 0xFFFD9C,
              9: 0xFFFDA2, 10: 0xFFFD90, 11: 0xFFFD96, 26: 0xFFFD12, 28: 0xFFFD0C, 30: 0xFFFD06}
for t in range(16):
    VECTOR_RAM[32 + t] = 0xFFFD18 + 6 * t
OTHER = 0xFFFDA8


def jump_table(reset_target):
    """vectors 1..47 as the 32X vector ROM sends them: 0x200 + 6 * (n - 1), jmp abs.l each"""
    out = bytearray()
    for n in range(1, 48):
        target = reset_target if n == 1 else VECTOR_RAM.get(n, OTHER)
        out += struct.pack(">HI", 0x4EF9, target)
    return out


def icd_block(crt0):
    s = open(crt0).read()
    i = s.index("Standard 32X startup code for MD side at 0x3F0")
    words = []
    for line in s[i:].split("\n")[1:]:
        t = line.strip()
        if t.startswith(".word"):
            words += [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", t)]
        elif words:
            break
    data = b"".join(struct.pack(">H", w) for w in words)
    assert len(data) == 0x410, "the 32X start-up block must be 0x3F0-0x7FF (%d bytes)" % len(data)
    return data


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    launcher, syms, cdl, crt0, out = sys.argv[1:]
    code = open(launcher, "rb").read()
    sym = {}
    for line in open(syms):
        p = line.split()
        if len(p) == 3:
            sym[p[2]] = int(p[0], 16)
    cdl_ofs = sym["cdl_code"] - 0x880000
    cdl_end = sym["cdl_code_end"] - 0x880000

    c = open(cdl, "rb").read()
    assert c[:13] == b"CD BOOT TABLE", "CDLOADER.32x: no CD boot table"
    dest, length, ment, sent, mvbr, svbr = struct.unpack(">6I", c[0x18:0x30])
    assert dest == 0x06000000, "CDLOADER is not linked at the start of SDRAM"
    assert cdl_end - cdl_ofs >= length, "CDLOADER's code in the launcher is shorter than its table says"

    # EDCD_SAVE=1: the header declares save RAM, so the EverDrive OS keeps its bank 31 (the battery RAM)
    # on the SD card as MEGA/gamedata/<this ROM>/bram.srm - 256 KB seen on a console with this entry
    # (edsave, 25 Oct 2026).  The listener reaches that bank as cart 0x800000 (hw_md.s MODE1-7).
    sram = b" " * 12
    if os.environ.get("EDCD_SAVE", "0") == "1":
        sram = b"RA" + bytes([0xF8, 0x20]) + struct.pack(">2I", 0x200001, 0x23FFFF)
    rom = bytearray(0x800)
    rom[0:4] = struct.pack(">I", 0x01000000)
    for v in range(1, 64):
        rom[4 * v:4 * v + 4] = struct.pack(">I", 0x3F0)
    hdr = (b"SEGA SSF        " + b"(C)MN99 2026.OCT"
           + b"CD32X CARTRIDGE BOOT: EVERDRIVE PRO".ljust(48) * 2
           + b"GM 00000000-00" + b"\0\0" + b"J6".ljust(16)
           + struct.pack(">4I", 0, 0x3FFFFF, 0xFF0000, 0xFFFFFF)
           + sram + b" " * 12 + b" " * 40 + b"JUE".ljust(16))
    assert len(hdr) == 0x100
    rom[0x100:0x200] = hdr
    jt = jump_table(0x880800)
    rom[0x200:0x200 + len(jt)] = jt
    rom[0x3C0:0x3F0] = (b"CD32X EDCD LOAD ".ljust(16)
                        + struct.pack(">8I", 0, cdl_ofs, 0, length, ment, sent, mvbr, svbr))
    rom[0x3F0:0x800] = icd_block(crt0)
    rom += code
    rom += bytes((-len(rom)) % 0x8000)
    s = sum(struct.unpack(">%dH" % ((len(rom) - 0x200) // 2), bytes(rom[0x200:]))) & 0xFFFF
    rom[0x18E:0x190] = struct.pack(">H", s)
    open(out, "wb").write(rom)
    print("%s: %d bytes, checksum %04X; CDLOADER %d bytes at ROM 0x%06X (master 0x%08X, slave 0x%08X)"
          % (out.split("/")[-1], len(rom), s, length, cdl_ofs, ment, sent))


if __name__ == "__main__":
    main()
