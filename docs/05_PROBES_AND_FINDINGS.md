# 05 – The probes, and what the console answered

Every rule in docs 01 came out of a ROM that asked the console one question. They are in `probes/`
with their sources; each builds on its own (`bash <probe>/build.sh`), needs no disc, and runs from
the EverDrive menu with the 32X attached. They share one base: Chilly Willy's 32X start-up, a 68000
side that serves numbered commands from the master SH2 (COMM0 = command, COMM2 = argument), and a
text screen on the 32X frame buffer. Only operations already proven on the console go into the next
probe - a dead console earlier in this work taught that.

## `edtest` – is it RAM? (24 Oct 2026)

Maps each of banks 1-27 into windows 1-7, the 68000 fills it with an address pattern (RV = 1), the
68000 reads it back, then both SH2s read it; the next pass inverts the pattern. Two builds: CTRL0
`0xA000` (`EDTEST_W`) and `0xE000` (`EDTEST_X`, with the X bit).

**Result:** `ALL TESTED BANKS OK`, 0 bad words, three passes, with both values. The 16 MB is RAM for
a 32X program; the mapper switches with the 32X on; the SH2s read what the 68000 wrote. Photos in
`probes/edtest/results/`.

An earlier form of it stopped with `68000 NOT ANSWERING`: the 68000's vblank handler clobbered
`d0`/`a0` and wrote the pads into COMM8/10. The probes keep the 68000's interrupts off since.

## `edw0` – window 0 as the SH2 reads it (25 Oct 2026)

After the cartridge boot copied the game correctly (verified by the 68000) and the game still died
within its first frames at random places, this ROM had the 68000 fill bank 0's upper half through
window 1 and the master SH2 read it back through **window 0**, while the 68000 did one thing at a
time: nothing; reading the cart; reading the VDP; reading the Sega CD; taking its vblank interrupt;
writing work RAM; writing the VDP; writing the gate array - with W on and with W off.

**Result (`EDW0E_W`):** perfect with the 68000 idle or reading anything; with W on, errors (zeros
read, and stored words changed) as soon as the 68000 *wrote* anything, even work RAM; with W off,
perfect through every write. That is rule 2, and the launcher that works came from it the same
evening (`EDCD H3`).

## `edsave` – what survives the menu (25 Oct 2026)

Writes a 32-byte marker at twelve places - bank 0 (`0x020000`, `0x040000`, `0x060000`, `0x07FFE0`),
bank 7 (`0x3F0000`, `0x3F0400`, `0x3FFFE0`) and bank 31 (`0`, `0x7FE0`, `0xFFE0`, `0x1FFE0`,
`0x3FFE0`) - and reports at every start what it finds: `NEW`, `RUN n OK`, `RUN n BAD k`. The header
declares save RAM (`RA`).

**Result, three runs with the menu in between:** banks 0 and 7 `NEW` every time (the one marker
just past the ROM's 128 KB came back with a word or two changed - the loader's tail); bank 31
`RUN 1 OK`, then `RUN 2 OK`; `MEGA/gamedata/EDSAVE1.32X/bram.srm`, 256 KB, on the card. Rules 4 and
the save bank.

## `edsave2` – the save bank as the listener works it, SH2 busy (25 Oct 2026)

With OpenLara frozen when its passport opened, this ROM does each access the listener makes - word
and byte writes with W on, byte and word reads with RV = 1 - as 4 KB blocks of bank 31 through
window 1, first with the slave SH2 idle, then with the slave reading window 4 nonstop as a game does,
plus bank 0 through window 1 and bank 31 through the 32X's own window (RV = 0) for comparison. Each
row ends `OK n`, `BAD n` or `HANG` (no answer in 10 s).

**Result:** all ten rows `OK`, 1-2 ticks each. The raw accesses are fine with a busy slave; the
freeze was not in the memory. (The screen's `SLAVE BAD` count is the probe's own bug - its fill of
window 4 ran with W off, so the slave compared against unwritten memory. The count is meaningless;
the rows are not.) That pointed back at the game, and the diagnostic launcher found the slave's PWM
DMA (docs 04, EDSAVE-2).

## The diagnostic launcher (`EDCD_DIAG=1`)

Not a probe but the same idea: the game's own launcher with the Sub-CPU's green text left on over
the 32X picture - the boot steps, the Sub-CPU heartbeat, the save commands traced on row 21
(`T62 P102 S3 57:2090 58:4096 61:2 62:23` = command 62, slot piece `0x102`, step 3 done, and how
many of each so far) and all eight COMM registers on rows 19-20 once a second in the game. Two
photos of a frozen screen settled the last freeze: the Sub-CPU alive and idle (`C0 0000`), the
game's frame counter stopped (`CA`), the last command complete.

## Chronology of 25 Oct 2026, for the record

1. `EDPROBE`: the C bit does nothing → the cartridge boot (`EDCD`).
2. `EDCD`: the 32X started, the Sub-CPU came up, the game's copy never landed (`4DE2`) → window 0
   cannot be written directly → the window-1 route.
3. The game started (`4E00`) and died at random early points. Probes eliminated the BIOS waits, the
   FM bit, the display mode, bank 0 versus the window, cached SH2 fetches, the X bit, 68000 reads of
   anything. `edw0` phases C-E: 68000 *writes* with W on.
4. `EDCD H3` (W off except inside copies): BootCamp and OpenLara run from the EverDrive. The disc's
   Sub-CPU program unchanged throughout; the stack's md5 checked after every change.
5. Saves did not persist → `edsave` → the PSRAM wipe, bank 31, `bram.srm` → EDSAVE-1.
6. The passport froze → the slot cache; froze again → `edsave2` (memory fine) → the diagnostic
   launcher → the slave's DMA → EDSAVE-2 (parking). Verified in ares; the console's turn.
