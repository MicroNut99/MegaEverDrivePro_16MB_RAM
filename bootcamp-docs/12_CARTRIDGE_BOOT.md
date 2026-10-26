# 12 – Cartridge boot: a CD32X disc started from the Mega EverDrive PRO

**Why.** A CD32X disc normally boots from the Sega CD with *no* cartridge in the slot (the CD boot, the Sega
CD's "mode 2"): BIOS at `0x000000`, Word RAM at `0x200000`, the RAM cart at `0x400000`. The EverDrive PRO
is a cartridge and stays one. On the console (25 Oct 2026, `edboot/EDPROBE.BIN`), writing CTRL0's C bit
changed nothing: the EverDrive stayed at `0x000000` and the Sega CD at `0x400000`, even after a 68000
RESET and a second write. So the console stays in **cartridge mode** ("mode 1"), the map reversed:

| | CD boot (normal) | Cartridge boot (EverDrive) |
|---|---|---|
| `0x000000` | Sega CD BIOS | **the cart** (EverDrive, 4 MB window of its 16 MB) |
| `0x200000` | Word RAM | the cart |
| `0x400000` | **the cart** | Sega CD BIOS + PRG-RAM window |
| `0x600000` | the cart | Word RAM |

**How.** `edboot/EDCD.32X`, a 32X cartridge ROM with a `SEGA SSF` header, does what the Sega CD BIOS and
the disc's boot block do in a CD boot, then hands over to the **same** stack:

1. The 32X starts the cartridge way: the cartridge's 32X header (`0x3C0`) has the boot ROMs load the
   stack's SH2 loader (CDLOADER). It waits for the listener, as in a CD boot.
2. The EverDrive's mapper as a 4 MB RAM cart: banks 0–7 in windows 0–7, writes on (`0xA000`).
3. The RAM jump table a CD boot's BIOS leaves at `0xFFFD00`. The cartridge's 68000 jump table (`0x200`)
   points there, so the listener's vblank works as in a CD boot.
4. The Sub-CPU: reset, its BIOS unpacked from the Sega CD's ROM into PRG-RAM (Chilly Willy's mode 1
   InitCD), a small start-up program at `0x6000` (`edcd_sp.s`) and the stack's Sub-CPU program at
   `0x8000`, then run. EDCD gives the Sub-CPU BIOS its level 2 interrupt every frame meanwhile.
5. The boot block's main-CPU program: the `I` / `B` / `M` handshake, then into Word RAM (`0x600000`).

Order matters on the console (25 Oct 2026): EDCD waits for the 32X boot ROMs' `M_OK` / `S_OK` before it
touches RV, and makes the mapper writes with RV = 1 (with the 32X on, register writes reach the cart only
then). Writing the mapper with RV = 0 left the EverDrive write-protected (`STATE:4DE2`); setting RV
while the boot ROMs were loading CDLOADER kept the SH2s from starting.

From there the stack runs unchanged except for **one switch**, `MODE1=1` (`stack/subcpu/Makefile`,
`hw_md.s`). It only changes the 68000 listener:
- every address argument it gets from the Sub-CPU is moved to the cartridge-boot map (Word RAM
  `0x2xxxxx` → `0x6xxxxx`, the cart `0x4xxxxx`–`0x7xxxxx` → `0x0xxxxx`–`0x3xxxxx`); 32X register
  values (commands 19–22) are left alone;
- every copy to or from the cart goes through a 1 KB buffer in work RAM: the Sega CD side (Word RAM) is
  read and written with RV = 0, the cart side with RV = 1 and interrupts off. On the console, reads of
  Word RAM made with RV = 1 came out garbled in cartridge mode (the same copies with RV = 0 did not);
  the 68000's vectors are the cart's while RV = 1, so no interrupt may come then either;
- the 32X is not reset and gets no CD boot table (it is running already).

Without `MODE1`, `hw_md.s` assembles to the same bytes as before (checked: the Sub-CPU program's md5 is
unchanged), so `stack/BOOT_FILES.md5` was updated for the new source only.

**The disc.** `EDCD=1 bash build.sh` builds the normal disc plus `edboot/EDCD.32X`. The disc's game ROM
gets one change (`tools/prep_rom.py --mode1`): its 68000 jump table points at the RAM table. A CD boot
never uses that table (its vectors are the BIOS's), so **the same disc boots both ways**: the normal
way with a RAM cart, and from the EverDrive with `EDCD.32X`. The Sub-CPU program inside `EDCD.32X` is a
second build of the stack with `MODE1=1`; the disc keeps the normal one.

**Two more things the console taught (25 Oct 2026), both in the listener's cart copies:**
- The cart's first 512 KB (window 0) cannot be written by the 68000 directly with the 32X on: the copy
  routes window 0's addresses through window 1 (CTRL1 = window 0's bank for the block, then bank 1 again).
- **The EverDrive's write-enable bit (CTRL0 bit 13, W) is off except inside a copy into the cart.**
  With W on, some of the 68000's writes to *other* addresses (work RAM, the VDP, the gate array - what a
  listener does all the time) land in the cart and change it, and the SH2's reads of the cart come back as
  zeros meanwhile (`edw0`, below).  So EDCD sets CTRL0 = `0x8000` (P only), and the listener turns W on
  right before a copy into the cart and off right after - with RV = 1 (the SH2 off the cart) and no other
  68000 write in between.  Reads of the cart leave W off.  Turning W on and off does no harm by itself.

**What the EverDrive keeps (25 Oct 2026, `edsave/EDSAVE1.32X`, three runs on the console).** The 16 MB of
banks 0–29 are PSRAM, and the EverDrive OS **wipes it every time its menu runs**: markers written in banks
0 and 7 were gone at the next start, every time.  Bank 31 is the EverDrive's battery RAM: its markers
survived every round trip, and the OS wrote `MEGA/gamedata/EDSAVE1.32X/bram.srm` (256 KB) for the ROM,
because its header declares save RAM (an `RA` entry).  krikzz's own SSF mapper source
(github.com/krikzz/MEGA-PRO, `fpga/mapper/map_ssf`) says the same: banks 0–15 and 16–30 are the two 8 MB
PSRAM chips, bank 31 the backup memory.  Consequences:
- a game's saves in the cart do not outlive a session.  **MODE1-7** (`EDCD_SAVE=1`): the listener reaches
  bank 31 as cart `0x800000`–`0x83FFFF` (through window 1, as window 0), and `make_edcd.py` gives EDCD the
  `RA` entry, so the OS keeps the bank on the SD card.  A game stores its saves there through the Sub-CPU
  (OpenLara CD32X REV, EDSAVE-1: CMD 61 writes it, a new CMD 62 reads it, since the SH2 cannot see that
  bank).  Only the bank's first 256 KB: the OS keeps its own things above.
- "is the game / level already in the cart?" checks can never hit after a relaunch; every launch loads
  from the CD.  The checks are harmless (they verify a checksum of the data), just never true.

**Switches** (all off by default): `M1WORDS=1` word copies; `EDCD_SAVE=1` the save bank (above); `EDCD_W0=<bank>` the game in that EverDrive bank
(window 0 shows it; the listener's window-1 route follows); `EDCD_X=1` CTRL0 with the EverDrive's X bit
(`0xC000`; it changed nothing on the console).  The diagnostic build has more (`M1MINVB`, `M1NOVINT`,
`EDCD_QUIET`), described in `build.sh`.

## Run it

Genesis + Sega CD + 32X, the EverDrive PRO in the 32X slot, the disc in the Sega CD. Copy `EDCD.32X` to
the SD card and start it from the EverDrive menu (not "Run Mega-CD").

| You see | Meaning |
|---|---|
| The game | It works |
| **Cyan** screen | the 32X did not start the stack's SH2 loader (6 s) |
| **Blue** screen | no Sega CD found |
| **Magenta** | no Sub-CPU BIOS found in the Sega CD's ROM |
| **Red** | the Sub-CPU did not start (10 s) |
| **Yellow** | the stack's Sub-CPU program did not hand over (30 s) |
| Black, nothing | the stack is running but stuck: try the diagnostic disc (`DIAG=1 EDCD=1 bash build.sh`) |

While EDCD runs, the 32X's COMM12 holds its progress: `ED01` Sega CD check, `ED02` Sub-CPU BIOS found,
`ED03` Sub-CPU started, `ED04` its start-up ran, `ED05` handing over. CDLOADER's own codes follow.

## Tested

**ares** (the CD32X build with the EverDrive cart, `--ramcart-type everdrive --ramcart-boot
edboot/EDCD.32X`): the BootCamp disc boots, the game runs (`STATE:4B10`), the pad and CD music work, the
data file loads from the CD into the cart, saves work. The same disc still boots the normal way with a
4 MB RAM cart.

ares quirk: with the 32X on, ares sends 68000 accesses to `0x000000`–`0x0000FF` to the 32X even with
RV = 1, so the diagnostic disc's cart probe (it writes cart offset 0) says `SAFE CART: NOT FOUND` there.
The game's first 256 bytes are the same as EDCD's (both from Chilly Willy's template), so loading is not
affected.

**Console (25 Oct 2026, Genesis + Sega CD + 32X, Mega EverDrive PRO):** the BootCamp disc boots from
`EDCD.32X` end to end - the 32X starts CDLOADER from the cart, the Sub-CPU comes up, the game is copied into
the EverDrive and verified, the hand-over, the game runs (`STATE:4B10`), it saves to the cart and loads its
data file from the CD into the cart; the pad works.  The release disc and the diagnostic disc both.

How it got there, for the record: with the 68000 writing window 0 directly the copy never landed
(`4DE2`); through window 1 it did, and the game started (`4E00`) but died within its first frames at random
places - a CPU exception in the SH2's interrupt code each time (the game's vector table now reports those
instead of swallowing them).  The SH2 image copied into SDRAM at the hand-over differed from the cart.  The
no-disc probe `edw0/` (from edtest) then measured the EverDrive's window 0 as the SH2 reads it: perfect with
the 68000 idle or reading anything, and failing - zeros, and the stored words changing - as soon as the
68000 *writes* anything with the write-enable bit on.  `edw0/EDW0E_W.32X` shows it in one screen.

**ares** did not model that EverDrive behaviour (its reads were right all along), which is why every
launcher passed there first.  It does now (the ares CD32X build, branch `everdrive-pro`, 25 Oct 2026): a 68000 write to
anything but the cart with W on disturbs an SH2 cart read within 1 µs of it (the read returns 0 and the word
takes the 68000's data), and a direct 68000 write to window 0 is dropped.  With that, the launcher that
works on the console (`EDCD_H3`) boots BootCamp and OpenLara in ares, and the earlier one with W always on
(`EDCD_DIAG_H2`) fails there as it did on the console.  The model is deterministic, not exact: the console
never said which word changes or how long the window is.
