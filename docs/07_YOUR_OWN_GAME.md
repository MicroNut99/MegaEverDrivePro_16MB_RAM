# 07 – `EDCD.32X` is a hook for *your* CD game: how to make one

`EDCD.32X` is not a generic tool. It is a 32X cartridge ROM that carries, inside it, three things
that belong to one disc:

| Inside the launcher | Where it comes from | Why it is disc-specific |
|---|---|---|
| the disc's **Sub-CPU program**, built with `MODE1=1` | the game's `subcpu/` | it serves that game's commands (its files, its sound, its saves); another game's disc has another one |
| the disc's **SH2 loader** (CDLOADER) | the game's `sh2/CDLOADER.32x` | it knows the game's ROM file name and load addresses |
| the **launcher code** (`edcd_md.s`, `edcd_sp.s`) | the CD32X BootCamp `edboot/` | the same for every game |

So: **one launcher per disc.** Start `EDCD.32X` from the EverDrive menu with *its* disc in the Sega
CD and the game runs from the EverDrive; start it with another disc and the Sub-CPU program in the
launcher will not match what the disc's SH2 side expects. Rebuild the launcher whenever the Sub-CPU
side changes; the disc itself needs no change for the launcher's sake (it boots both ways).

## What a game needs to have

The hook works for games built on the **CD32X BootCamp boot stack** - the stack that Kobo, OpenLara,
OpenJazz, Hurrican, SuperTux, OutRun and Sonic CD 32X use: a Sub-CPU program (`subcpu/main.c` +
the 68000 listener `hw_md.s`), an SH2 loader (CDLOADER) and a game ROM that CDLOADER copies into
the RAM cart. The launcher reproduces the stack's CD boot in cartridge mode; everything above the
stack (the game) does not know the difference.

A Sega CD 32X game that is **not** on this stack - an original title, a port with its own loader -
has its own boot sequence written for the CD boot's memory map (BIOS at `0x000000`, Word RAM at
`0x200000`). The launcher cannot run that: the cartridge map is the reverse (docs 02). Such a game
has to be moved onto the stack first (the BootCamp's `docs/02_PORTING_YOUR_GAME.md`) - the stack's
job is precisely to be the one boot everybody shares.

## Making the launcher for your game

1. **Your game builds on the stack.** Its `subcpu/hw_md.s` is the stack's listener *with the MODE1
   switches* (`listener/hw_md.s` here, or the BootCamp's `stack/subcpu/hw_md.s`; OpenLara's is the
   same file). Its `subcpu/Makefile` has the `MODE1` block (`listener/Makefile`, the `ifeq ($(MODE1),1)`
   part: `M1WORDS`, `M1MINVB`, `M1NOVINT`, `M1W0BANK`, `M1CTRL0`, `M1SAVE`). If your game's listener
   is an older copy of the stack's, take the stack's: the MODE1 code is all under switches and the
   normal build assembles byte for byte as before (the kit checks that).

2. **The game ROM's jump table** points at the RAM table: `tools/prep_rom.py --mode1` when the ROM is
   prepared for the disc (OpenLara's `tools/prep_openlara_cd.py --mode1` does the same). A CD boot
   never uses that table, so the one disc boots both ways.

3. **Build the Sub-CPU program a second time, for the cartridge boot**, in a copy of `subcpu/` so the
   disc keeps the normal one:

   ```bash
   cp -r subcpu /tmp/subcpu_m1 && cd /tmp/subcpu_m1 && make clean
   make CDROMPlayer.bin MODE1=1 M1CTRL0=0x8000            # + M1SAVE=1 EDSAVE=1 for the save bank
   ```

4. **Wrap it**, with the CD32X BootCamp's builder:

   ```bash
   EDCD_SAVE=1 bash <CD32X-BootCamp>/edboot/build_edcd.sh /tmp/subcpu_m1/CDROMPlayer.bin sh2/CDLOADER.32x EDCD.32X
   ```

   `build_edcd.sh` assembles `edcd_md.s` and `edcd_sp.s`, and `make_edcd.py` lays the ROM out: the
   header (`SEGA SSF`; `EDCD_SAVE=1` adds the `RA` save-RAM entry so the EverDrive OS keeps `bram.srm`),
   the 68000 jump table, **the 32X header at `0x3C0` copied from CDLOADER's own CD boot table** - that
   is the hook: the 32X boot ROMs load your CDLOADER from the cartridge exactly as the Sega CD BIOS
   would from the disc - Sega's security code, then the launcher and the two embedded programs.

   OpenLara's `build.sh` does steps 3 and 4 for you (`EDCD=1`), and the BootCamp's `build.sh` likewise.
   Copy that `EDCD=1` block into your game's `build.sh`: it is twenty lines.

5. **Name it.** The EverDrive keeps the save file under the ROM's name
   (`MEGA/gamedata/<name>.32X/bram.srm`), so the name you give the launcher is the name of the save.

6. **Copy it to the SD card**, the disc in the Sega CD, start it from the EverDrive menu (not
   "Run Mega-CD"). The launcher's screen colour says where it stopped if it stops (docs 02).

## If your game saves

A save written into the RAM cart is gone at the next launch (the PSRAM wipe, docs 04). To keep saves
on the EverDrive:

- the Sub-CPU writes them to the save bank: the cart address `0x800000 + offset` in a
  `MD_CMD_CHILLY_RV_MEMCPY`, with the listener built `M1SAVE=1` (OpenLara's `CMD 61` is the model,
  `openlara/subcpu/main.c`);
- the SH2 side reads them back through the Sub-CPU, since no window shows the bank to the SH2s
  (OpenLara's `CMD 62` + `57`);
- **park the slave SH2 and stop anything of yours that DMAs from the cart before every such copy**
  (`MARS_WAIT` and your mixer's stop), as the level loads do - or the SH2s lock;
- cache what the menu draws every frame.

A game whose saves are small enough can use the Sega CD's own backup RAM instead (the stack's
`CMD 56`-`59`, 8 KB in all): nothing on the EverDrive to think about then.

## What stays the same for every game

The three rules of docs 01 are the listener's business, not yours: window 0 through window 1, the
write bit only inside copies, the buffer between Word RAM and the cart. The game's copies into the
cart (`CMD 46`, `61`) go through them unchanged.
