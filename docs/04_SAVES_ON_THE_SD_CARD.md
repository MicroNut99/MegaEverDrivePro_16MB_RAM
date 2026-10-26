# 04 – Saves on the SD card (EDSAVE-1 and EDSAVE-2)

## The problem

OpenLara CD32X keeps its settings and its 16 save slots in the RAM cart: the settings and the old
single save in the cart's last 64 KB (`0x3F0000`), the slots at `0x060000`-`0x07FFFF`. On a
battery-backed cart they outlive power-off. On the EverDrive they are in the PSRAM, and
**the EverDrive OS wipes the PSRAM every time its menu runs** (`edsave`, three round trips: banks 0
and 7 blank at every restart, bank 31 intact). A save made with the EverDrive worked within the
session and was gone at the next launch.

## Where they go now

**Bank 31**, the EverDrive's battery RAM. With the `RA` entry in the launcher's header (what
`EDCD_SAVE=1` adds), the EverDrive OS writes the bank's first 256 KB to the SD card as
`MEGA/gamedata/<launcher name>/bram.srm` and loads it back at the next launch (seen on the card after
the probe's second run: `bram.srm`, 256 KB). Layout in the bank, the same piece numbers as before:

| Bank offset | What |
|---|---|
| `0x00000` | the 64 KB save area: `OLST` settings at 0, the old single save at `0x400`, the fast-load records in piece 14 |
| `0x10000` | the 16 save slots, 8 KB each (`OLGM`, size, SaveGame, data) |

The SH2s cannot see bank 31 (it is never in a window the game uses), so **every access goes through
the Sub-CPU**, and the Sub-CPU through the 68000 listener's MODE1-7 route (docs 03).

## The commands (`openlara/subcpu/main.c`, built with `EDSAVE=1`)

| Command | Direction | What it does |
|---|---|---|
| `58` put | SH2 → Sub-CPU | one word of the 4 KB buffer (COMM2 = index, COMM8 = value) - as before |
| `61` to cart | Sub-CPU → bank | COMM2 = piece (bit 8 = a slot piece, bits 0-4 the number): buffer → Word RAM → `RV_MEMCPY` to `0x800000 + offset` (the listener: window 1 = bank 31, W on for the copy) → read back the same way → compared. Reply COMM12 `0x301` = ok, slot area, save bank |
| `62` piece in | bank → Sub-CPU | **new**: COMM2 = piece → `RV_MEMCPY` from `0x800000 + offset` to Word RAM → the 4 KB buffer. COMM2 = `0x8000` asks only what this Sub-CPU does: `1 | 0x100` (slots) `| 0x200` (the saves are in the bank) |
| `57` get | Sub-CPU → SH2 | one word of the buffer (COMM2 = index → COMM8) - as before |

Without `EDSAVE=1` (the disc's own Sub-CPU program, booted the normal way with another cart), `61`
writes the cart as before and `62` answers without the `0x200` bit: the game then reads the cart
directly.

## The engine (`openlara/engine/openlara-edsave.patch`, applied with `EDCD_SAVE=1`)

- At the first read of the save area the game asks (`62`, `0x8000`). With `0x200`: every
  `cartRead` of the save area becomes `62` (the piece into the Sub-CPU's buffer, cached by piece
  number) + `57` per word. Writes already went through `58` + `61`.
- The old "probe" that wrote slot piece 31 back to find out whether the Sub-CPU had slots is skipped:
  the query answers that.
- **Slot cache** (EDSAVE-1b): the passport draws every listed slot's level *every frame* - 16 slots
  on the Save page - and each was a round trip per piece and per word. The levels are read once and
  kept; a write to the save area forgets them. Without this the Save page brought the game to a
  crawl on the console.
- **SH2 parking** (EDSAVE-2): before every `61` and `62` the master waits for the slave to finish
  its command (`MARS_WAIT`) and stops the slave's PWM mixer (`sfxBankStop`, its DMA reads the
  sound bank out of the cart); after the command the mixer restarts (`sfxBankStart`, if it was
  on). Nested, so a run of commands parks once. This is exactly what the level loads always did
  before their copies - the saves did not, and on the console a 68000 copy with RV = 1 while the
  slave drew and its DMA read the cart locked both SH2s, every time the passport opened (`T62 P102
  S3` as the last trace line, the frame counter stopped, the Sub-CPU and the 68000 idle).

## What a save costs

A piece write is 2048 × `58` + `61` (as before); a piece read 1 × `62` + the words needed. Opening
the Save page the first time reads 16 slot headers (16 × `62` + a few `57`); a save reads one piece
(the read-modify-write of the slot's first piece: 2048 × `57`) and writes it. A short gap in the
sound effects while the slave is parked.

## Fast loading

The "is this level already in the cart?" records live in the save area too (piece 14) and so
survive a relaunch - but the level data in the PSRAM does not, and the record's checksum of the
data correctly fails. Every launch loads from the CD. Within a session (title → level → back → the
same level) the records work as before.

## Where it stands (26 Oct 2026)

With the engine patch in the disc (the first discs had it applied *after* the engine was compiled -
a `build.sh` anchor mistake, since fixed): the save lands in bank 31, survives the EverDrive menu and
loads from the title's passport on the console. **Opening the passport in the game still freezes**
(music on, the Sub-CPU idle, the last command complete - the master SH2 dies right after a save
command returns, never at the title). EDSAVE-3: two launcher-only variants to split it -
`EDCD_SAVEBANK=27` (a PSRAM bank: is it the battery RAM?) and `EDCD_SAVEWIN=7` (is it window 1,
where the Caves data is, being remapped under the game?). See docs 03, MODE1-7b.

## Checking it

- On the console: save in a slot, reset to the menu, relaunch, Load Game lists it;
  `MEGA\gamedata\<launcher>\bram.srm` on the card, dated.
- In ares: the same run with the launcher; the save shows up in the EverDrive cart file at
  bank 31 (`31 << 19`): `OLGM` at `+0x10000` for slot 1. `probes/` has nothing for this - it needs
  the game.
