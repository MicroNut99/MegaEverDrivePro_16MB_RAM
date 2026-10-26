# 01 – How it boots

From power-on to your game's `main()`. Sources: `stack/subcpu/` (crt0.s, main.c, hw_md.s, scd.c) and
`stack/sh2/cdloader_sh2.c`. Background: *KOBO-CD32X_BOOT_PROCESS* and the command ledger in
https://github.com/MicroNut99/docs.

## The four processors

| Processor | Program | Its job once the game runs |
|---|---|---|
| **Sega CD Sub-CPU** (68000, 12.5 MHz) | `APP.BIN` (`stack/subcpu`) | The coordinator. It reads the CD, plays CD audio and the PCM chip, keeps the backup RAM, and answers the SH2's commands. |
| **Genesis 68000** (7.6 MHz) | the listener in `hw_md.s`, copied to its RAM at `0xFF1000` | A command slave of the Sub-CPU. It reads the pads, reaches the 32X registers, writes the RAM cart (RV = 1) and drives the Genesis VDP. It never runs your cartridge's 68000 code. |
| **32X master SH2** (23 MHz) | your game (first CDLOADER) | Runs the game. It sends every request to the Sub-CPU. |
| **32X slave SH2** | your game (first CDLOADER) | Whatever your game gives it. In the example it is idle. |

**Why the commands exist:**

- The SH2s cannot reach the CD or the Sega CD hardware.
- The Sub-CPU cannot reach the 32X or the Genesis VDP.
- The Genesis 68000 reaches both, but has no CD.

So a request travels SH2 → 32X COMM registers → Sub-CPU → Sega CD comm registers → Genesis 68000 →
hardware, and the reply comes back the same way.

## Step by step

### 0. The disc
`stack/subcpu/Makefile` (target `cd`) runs `genisoimage -generic-boot US_BOOT.BIN`. The boot block is the
toolchain's generic Sega CD boot sector (security code, IP for the Genesis, SP for the Sub-CPU). It is
**not** part of this kit, and it never changes.

| File on the disc | What it is |
|---|---|
| `APP.BIN` | The Sub-CPU program. CDLOADER is built into it (`files.s`, `.incbin "ROMS/CDLOADER.BIN"`). |
| `GAME.32X` | Your ROM: file 20, made by `tools/prep_rom.py`. |
| `IMAGE.RAW` | The boot picture: file 0, 320 × 224 + palette. |
| Your data files | As listed in `game_files.h`. |
| Tracks 2… | CD audio. Disc track N = what CMD 45 N plays. |

### 1. BIOS and boot block
The Sega CD BIOS checks the disc's system area and starts the boot block. The boot block's SP loads
`APP.BIN` into Sub-CPU program RAM and calls `_start` (`crt0.s`). Its IP keeps the Genesis 68000 waiting
for code in Word RAM.

### 2. The Sub-CPU starts (`crt0.s`, `main.c`)

1. `_start` clears BSS, sets a stack and calls `init_hardware`. That function copies the Genesis code
   (`md_init_start` … `md_init_end` in `hw_md.s`) to Word RAM.
2. `main()` hands Word RAM to the Genesis.

### 3. The Genesis 68000 becomes the listener (`hw_md.s`)

1. It copies itself to `0xFF1000` and sets up the VDP (font, palette). It then switches the display **off**
   (`hw_md.s`, `OL-13`), so no boot text shows unless a game turns it on again (CMD 52).
2. It installs `vblank_int`. Every frame that handler:
   - reads pads 1 and 2 into the Sega CD comm words;
   - counts ticks;
   - raises the Sub-CPU's level-2 interrupt.
3. It then loops in `cmd_loop`, waiting for numbered commands from the Sub-CPU.

The table (`cmd_table` in `hw_md.s`) has routines 1–35. The enum in `hw_md.h` is an old leftover and stops at 20; `main.c` defines the numbers it uses itself.

### 4. The 32X wakes up (`MD_CMD_INIT_32X`)
The Sub-CPU copies the SH2 program (CDLOADER) into Word RAM and sends `INIT_32X`. The 68000 then:

1. checks for `"MARS"` at `0xA130EC`;
2. sets ADEN and releases the SH2 reset;
3. clears both frame buffers and the palette;
4. copies CDLOADER into the frame buffer;
5. gives the frame buffer to the SH2s and writes `"_CD_"` to COMM0.

The 32X boot ROMs see `"_CD_"`, copy the program into SDRAM `0x06000000`, answer `M_OK` / `S_OK`, and start
it. CDLOADER has its own CD boot table in `cdl_crt0.s`.

### 5. The handshake (COMM10)
CDLOADER writes `0x0AAA`. The Sub-CPU answers `0x0BBB` after checking the cart. Each value stays in COMM10
until the other side has read it, so the order does not matter. The Sub-CPU waits at most 180 ticks
(about 3 s) for `0x0AAA`; then it stops (`SH2 NEVER SIGNALLED READY`, [09](09_ERROR_CODES.md)).

### 6. CDLOADER loads your game (`stack/sh2/cdloader_sh2.c`)
Status codes appear in COMM12. The Sub-CPU writes them as `STATE:` in its diagnostic text, which shows only
once a game turns the Genesis display on. Every code is in [09](09_ERROR_CODES.md).

| Code | Step |
|---|---|
| `4D01` | Started; the Genesis text is switched off (CMD 52) and `IMAGE.RAW` is shown (CMD 38 file 0, then CMD 51 per chunk into the frame buffer) |
| `4D06` | CMD 38 file 20 opens `GAME.32X`; CMD 47 compares its first, middle and last chunks with the cart. This loader ignores the answer (quick check off, `4D07` = "already there" is never posted) and always copies. |
| — | CMD 46 copies each 32 KB chunk into the cart at offset 0; the Sub-CPU reads every chunk back and compares it. |
| `4C00`+n | Chunk n loaded |
| `4D02` | All chunks loaded and verified |
| `4D03` | The ROM header checks out: `SEGA` at `0x100`; at `0x3D4` the SDRAM image (source, destination, size ≤ 192 KB); the two entries' top byte is `0x06` (SDRAM). The VBRs are not checked. |
| `4D04` | The slave is parked in a small routine at the top of SDRAM (`0x2603C000`) |
| `4D05` | Hand-over |
| `4DE1` | Error: `GAME.32X` not on the disc |
| `4DE2` | Error: a chunk failed its read-back |
| `4DE3` | Error: not a 32X ROM, or a bad header |
| `4DE4` | Error: the slave did not park |

**The hand-over:**

1. With interrupts off, the routine at the top of SDRAM copies your SDRAM image over CDLOADER.
2. It purges the caches.
3. It writes 0 to COMM0 and COMM4. A cartridge's 68000 would write `M_OK` / `S_OK` here; on the CD32X the
   68000 stays in the listener, so your `crt0.s` does not wait.
4. It releases the slave, and both SH2s jump to your entries with your VBRs and stacks.

**The 68000 is never handed to your ROM.** CMD 60, which does that, exists only for the D32XR experiment.
It stays the Sub-CPU's listener, so every CD service keeps working while the game runs.

### 7. Your game runs
Your `crt0.s` (Chilly Willy's standard one, see `hello/src/crt0.s`):

1. clears `.bss`;
2. turns the cache on;
3. calls `main()` on the master and `secondary()` on the slave.

Your code runs from the cart (`0x02000000`, read like a cartridge ROM). Your `.data` and `.bss` live in
SDRAM.

From here the Sub-CPU's main loop serves your commands. Post `0x4B10` in COMM12 when your game is
running; the diagnostics treat it as "game running".

## Two other ways the same base is used
- **Kobo Deluxe** (the root of this stack) has no ROM loader: the whole game is the SH2 program built into
  `APP.BIN`, running from SDRAM.
- **Sonic CD 2011** embeds a loader too, but a different one. It loads a headerless engine to cart 0,
  stays resident in SDRAM and calls the engine.

See [07](07_METHODS_COMPARED.md).
