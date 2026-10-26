# 04 – Memory maps

## The SH2s' view

| Address (cached / cache-through) | What | Notes |
|---|---|---|
| `0x02000000` / `0x22000000` | **the RAM cart**, 4 MB | Read only for the SH2s. Your ROM is at offset 0. Reads wait while the 68000 has RV = 1. |
| `0x04000000` / `0x24000000` | frame buffer (128 KB), overwrite image at `+0x20000` | 16-bit writes only |
| `0x06000000` / `0x26000000` | SDRAM, 256 KB | your `.data`, `.bss`, stacks, hot code |
| `0x20004000` | 32X system registers | COMM0–COMM14 at `0x20004020`–`0x2000402E` |
| `0x20004100` | VDP registers | |
| `0x20004200` | palette (256 × 16 bit) | |

**SDRAM at the hand-over** (`stack/sh2/cdloader_sh2.c`):

| SDRAM | During boot | Once your game runs |
|---|---|---|
| `0x06000000`–`0x06037FFF` | CDLOADER | **your SDRAM image** (destination + size ≤ `0x38000`, size ≤ 192 KB) |
| `0x0603C000` | the hand-over routine; the slave is parked here | free |
| `0x0603DF00` | CDLOADER's info block | free |
| up to `0x06040000` | | the stacks: Chilly's `crt0.s` puts the master at `0x0603F800` and the slave at `0x06040000` (2 KB for the slave: give it its own stack if it does real work, as Hurrican does) |

**`.bss` is not checked by CDLOADER.** The 192 KB / 224 KB limits cover the SDRAM image (`.data`) only.
Your `crt0.s` clears `.bss` at once. If `.bss` reaches `0x0603C000`, it overwrites the hand-over routine
that the slave may still be running. Keep `.data` + `.bss` below `0x0603C000` (the stacks sit above it),
and check `build/output.map`.

## The RAM cart (4 MB, battery-backed)

**Who writes it.** Only the Genesis 68000, with RV = 1, at its `0x400000`–`0x7FFFFF` (Mega CD mode). The
SH2s read it at `0x02000000`. The 68000 reads it back through the 32X windows: `0x880000` holds the first
512 KB; `0x900000` holds a 1 MB bank picked by `0xA15104`.

**Slots.** CMD 38's file number picks the slot: slot n = cart `n × 128 KB`. `game_files.h` maps the number
ranges onto slots.

### This kit's default layout (`game_files.h`)

| Cart | SH2 address | What | File numbers |
|---|---|---|---|
| `0x000000`–`0x07FFFF` | `0x02000000` | `GAME.32X`, your ROM (≤ 512 KB) | 20 |
| `0x080000`–`0x1FFFFF` | `0x02080000` | resident slot (slot 4, 1.5 MB) | 200 + n |
| `0x200000`–`0x3EFFFF` | `0x02200000` | level slot (slot 16, 1984 KB) | 100 + n, 300 + n |
| `0x3F0000`–`0x3FFFFF` | `0x023F0000` | the shared save area: 16 pieces of 4 KB | CMD 58 + 61 |

Other file numbers load `IMAGEn.RAW` to slot n. Number 16 is `KOBOGFX.BIN`.

- Slots 32 and up are not the cart: the 68000 would write into the 32X's own area (slot 32 = its `0x800000`,
  slot 34 = the frame buffer). `check_boot.py` allows slots 4–31 only.
- The game file list may hold at most 100 names. With a 101st, number 200 would open the 101st file into
  the level slot (the 100 + n range is checked first), and resident file 0 could not be reached.

`build.sh` reads the ROM name and both slots from `game_files.h` and works out the limits from them. With
the default layout it refuses:

- a `GAME.32X` over 512 KB;
- a data file over 1984 KB.

It warns about a data file over 1.5 MB, which can then only be loaded as 100 + n.

### The layouts of the existing games

| Cart | OpenLara REV | OpenJazz REV | Hurrican | SuperTux REV | Sonic CD 2011 |
|---|---|---|---|---|---|
| `0x000000` | engine (≤ 384 KB) | engine (≤ 512 KB) | engine | engine | engine `SONICCD.PAK` (headerless, ≤ 512 KB) |
| `0x060000` | 16 save slots (32 × 4 KB, CMD 61 bit 8) | | | | |
| `0x080000` | Caves (title level), resident | `GBnnn.PAK` Genesis plane pack (≤ 512 KB) | | | scene image (up to 2.5 MB) |
| `0x100000` | ↓ | `IMAGE8.RAW` common pack (≤ 1.5 MB) | `IMAGE8.RAW` resident pack | `IMAGE8.RAW` | ↓ |
| `0x120000` | ↓ | ↓ | ↓ | `IMAGE9.RAW` (area A) | ↓ |
| `0x200000` | the level being played, or the movie ring | ↓ | ↓ | ↓ | ↓ |
| `0x280000` | ↓ | world / bonus pack `OJnnn.PAK` | stage pack `OJnnn.PAK` | ring of `OJnnn.PAK` homes (area B) | ↓ |
| `0x300000` | ↓ | ↓ | ↓ | ↓ | data pack `SCDDATA.PAK` |
| `0x380000` | ↓ | ↓ | ↓ | ↓ | act file |
| `0x3F0000` | save area (pieces 0, 14) | save area (pieces 0–10) | save area (piece 10) | save area (piece 13) | — (backup RAM only) |

↓ = the area above continues.

## The save area: who owns which piece
One piece, one owner, on every cart: a player's cart holds the saves of every CD32X game they own. The
master list is §5 of `CD32X_COMMAND_LEDGER_ADDITIONS.md` in MicroNut99/docs.

| Piece | Cart | Owner |
|---|---|---|
| 0 | `0x3F0000` | OpenLara (settings `0x3F0000`, saved game `0x3F0400`) **and** OpenJazz `openjazz.cfg`. The two share it only one at a time. |
| 1–4 | `0x3F1000` | OpenJazz `SAVE.0`–`SAVE.3` |
| 5–9 | `0x3F5000` | Holiday Hare '95 (OpenJazz) |
| 10 | `0x3FA000` | OpenJazz port settings. Hurrican writes its settings here too: its port layer is OpenJazz's. |
| 11 | `0x3FB000` | **free** |
| 12 | `0x3FC000` | **free**. `hello` borrows it for its boot counter (magic `0x4243`). Take another for your game. |
| 13 | `0x3FD000` | SuperTux REV ("STX2") |
| 14 | `0x3FE000` | OpenLara REV load records (`OLCV`, `OLLV`) |
| 15 | `0x3FF000` | test scratch (cart-size probes). Never store anything here. |

**Writing.** Fill the Sub-CPU's buffer with CMD 58 (a word at a time), then CMD 61 = piece.

**Reading.** Read the piece directly: `0x223F0000 + piece × 4096`, cache-through.

**Backup RAM.** The Sega CD's internal backup RAM is the other place to save (CMD 56–59, one 4 KB file
named `GAME_BRAM_NAME`). Sonic CD 2011 saves there, and Hurrican keeps its high scores there. SuperTux
reports that backup-RAM saves never worked on its console. Test both on yours.

## 8 / 16 MB paged carts
These need an 8 / 16 MB paged cart board and a `CART=8` / `CART=16` build. The cart shows as two 2 MB windows.

- **Window 0** (`0x000000`–`0x1FFFFF`): never paged. Your ROM lives here.
- **Window 1** (`0x200000`–`0x3FFFFF`): pages 1–7, picked with CMD 68. The page register is `0xA130F2`;
  window 0's is `0xA130F0`.

After a reset, window 1 shows page 1, which is what a 4 MB cart looks like. Purge the caches after a page
change. A 4 MB cart ignores the page writes.

This is emulation-proven only: see `CD32X_SRAM_PAGING_PROPOSAL.md` and `CD32X_PAGING_SONICCD.md` in
MicroNut99/docs.

## The Sega CD side (for when you add a command)

| Sub-CPU address | What |
|---|---|
| `0x000000`–`0x07FFFF` | PRG-RAM: `APP.BIN` (linked at `0x8000` by the toolchain's `ldscripts/cd.ld`), its buffers |
| `0x080000` (2M mode) / `0x0C0000` (1M mode) | Word RAM, shared with the Genesis 68000 at `0x200000`. A CD chunk is read here, and then the 68000 copies it to the cart (MD 26/27/33). |
| `0xFF8000` | Sega CD gate array: the comm registers, the graphics (stamp) chip |
| `0xFF0000` | the PCM chip |

| Genesis 68000 address | What |
|---|---|
| `0xFF1000` | the listener (`hw_md.s`) |
| `0xA12000` | Sega CD comm registers (to and from the Sub-CPU) |
| `0xA15100` | 32X registers: `0xA15104` cart bank, `0xA15120` COMM0 … |
| `0xA130F0` / `0xA130F2` | the page registers of the 8/16 MB cart |
| `0xC00000` | Genesis VDP |

**Word RAM modes.** In 2M mode one side owns all of Word RAM. The bonus floors (OpenJazz) and the stamp
floor (Sonic) need it. **Rule** (Sonic CD 2011): in 2M mode the Sub-CPU must not swap banks; its next Word
RAM access hangs.
