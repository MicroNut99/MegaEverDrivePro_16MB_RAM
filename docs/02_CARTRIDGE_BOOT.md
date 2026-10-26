# 02 – The cartridge boot: `EDCD.32X`

The CD32X BootCamp's `docs/12_CARTRIDGE_BOOT.md` (kept as `bootcamp-docs/12_CARTRIDGE_BOOT.md`) is the
full account. This is the shape of it, with the 32X hook spelled out.

## Why not a CD boot

A CD32X disc normally boots from the Sega CD with *no* cartridge: the BIOS at `0x000000`, Word RAM
at `0x200000`, the RAM cart at `0x400000`. The EverDrive PRO is a cartridge and stays one: its `C`
bit, which the mapper documents as "release CART_IN", changes nothing on a 32X console
(`EDPROBE.BIN`, 25 Oct 2026 - the EverDrive stayed at `0x000000` and the Sega CD at `0x400000`,
through a 68000 RESET and a second write). "Run Mega-CD" from the EverDrive menu is not it either:
that gives the disc the EverDrive's backup-RAM cart function, not its 16 MB (`SAFE CART: NOT FOUND`).

So the console runs in **cartridge mode** (the Sega CD's "mode 1"), the map reversed:

| | CD boot (normal) | Cartridge boot (EverDrive) |
|---|---|---|
| `0x000000` | Sega CD BIOS | **the cart** (the EverDrive's 4 MB window on its 16 MB) |
| `0x200000` | Word RAM | the cart |
| `0x400000` | the cart | Sega CD BIOS + PRG-RAM window |
| `0x600000` | the cart | Word RAM |

## What `EDCD.32X` is

A 32X cartridge ROM (`SEGA SSF` header, so the EverDrive turns its mapper on) that does what the Sega
CD BIOS and the disc's boot block do in a CD boot, then hands over to the disc's **unchanged** boot
stack. `launcher/make_edcd.py` assembles it from three parts:

| Offset | Part | From |
|---|---|---|
| `0x000` | 68000 vectors (reset `0x01000000`, every other vector to `0x3F0`) | `make_edcd.py` |
| `0x100` | the ROM header: `SEGA SSF`, names, `GM 00000000-00`, ROM `0`-`0x3FFFFF`, RAM `0xFF0000`; with `EDCD_SAVE=1` the save-RAM entry `RA F8 20 00200001 0023FFFF` | `make_edcd.py` |
| `0x200` | the 68000 jump table (`jmp` entries at the 32X's fixed offsets, pointing into work RAM at `0xFFFD00`) | `make_edcd.py` `jump_table` |
| `0x3C0` | **the 32X hook**: `CD32X EDCD LOAD` + the stack's CD boot table (destination `0x06000000`, source offset in this ROM, length, master/slave entry, VBRs) | the stack's `CDLOADER.32x` header (its first `0x38` bytes) |
| `0x3F0` | Sega's 32X start-up and security code (the `MARS` block, as in Chilly Willy's crt0) | `edtest/src/crt0.s` |
| `0x800` | the 68000 program `edcd_md.s` (runs at `0x880800`, the cart through the 32X window), with `edcd_sp.s` (the Sub-CPU start-up) and the stack's MODE1 Sub-CPU program embedded, and CDLOADER's code | `edcd_md.s`, `edcd_sp.s`, the build |

**The 32X hook.** When the 32X boots a cartridge, its boot ROMs run the security code at `0x3F0`,
then look at the cartridge's 32X header at `0x3C0`: where the SH2 program is in the ROM, how long it
is, where to put it in SDRAM and where each SH2 starts. `make_edcd.py` fills that header from the
stack's own SH2 loader (CDLOADER - the same binary a CD boot's BIOS loads from the disc), so the boot
ROMs copy CDLOADER into SDRAM and start both SH2s on it. CDLOADER then waits for the 68000 listener,
exactly as in a CD boot. Nothing on the SH2 side knows it was started from a cartridge.

**The 68000 program** (`edcd_md.s`), in order - and the order matters:

0. Waits for the 32X boot ROMs' `M_OK` / `S_OK` before touching RV or the mapper (RV = 1 while they
   loaded CDLOADER kept the SH2s from starting).
1. The EverDrive's mapper as a 4 MB RAM cart: windows 1-7 = banks 1-7, CTRL0 = `0x8000` (P only: the
   write bit stays **off**, see rule 2). The register writes are made with RV = 1, from work RAM
   (RV = 1 takes the cart away from `0x880000`, where the program runs).
2. The RAM jump table a CD boot's BIOS leaves at `0xFFFD00` (the listener puts its vblank there; the
   cartridge's jump table at `0x200` points at it).
3. The Sub-CPU: reset, its BIOS unpacked from the Sega CD's ROM into PRG-RAM (Chilly Willy's mode 1
   `InitCD`), the small start-up program `edcd_sp.s` at `0x6000` and the stack's Sub-CPU program at
   `0x8000`, then run. The 68000 gives the Sub-CPU BIOS its level 2 interrupt every frame meanwhile.
4. The boot block's main-CPU program: the `I` / `B` / `M` handshake, then into Word RAM
   (`0x600000`), where the listener starts.

If it stops, the screen colour says where: **cyan** the 32X did not start CDLOADER, **blue** no Sega
CD, **magenta** no Sub-CPU BIOS in the Sega CD's ROM, **red** the Sub-CPU did not start, **yellow**
the Sub-CPU program did not hand over.

## What the disc needs

One change in the disc's game ROM (`tools/prep_rom.py --mode1`, done by the build): its 68000 jump
table points at the RAM table instead of the BIOS's. A CD boot never uses that table, so **the same
disc boots both ways** - the normal way with a RAM cart, and from the EverDrive with `EDCD.32X`.
The Sub-CPU program inside `EDCD.32X` is a second build of the stack with `MODE1=1` (docs 03); the
disc keeps the normal one.

## Then the stack runs

CDLOADER asks the Sub-CPU for the game's ROM file, the Sub-CPU reads it from the CD into Word RAM
chunk by chunk, the listener copies each chunk into the cart (through the rules of docs 01 and 03),
CDLOADER verifies, copies the SH2 image to SDRAM and starts the game. From there the game talks to
the Sub-CPU as on any CD32X disc: files, pad, CD audio, PCM, saves. The EverDrive is the RAM cart
it was waiting for - 16 MB of it.
