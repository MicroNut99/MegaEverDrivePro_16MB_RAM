# 11 – A 16 MB RAM cart for ares: the Mega EverDrive PRO

**Who this is for:** whoever adds a 16 MB RAM cart to the ares emulator (v148), a developer or an AI. It has
everything that work needs: what the hardware does (and how sure we are), where it plugs into ares, the
mistakes that are easy to make, and the tests that prove it is done.

**There is a working implementation:** [`ares/everdrive-pro-ares-v148.patch`](../ares/everdrive-pro-ares-v148.patch)
applies to upstream ares v148 (`git apply`, tag `v148` of https://github.com/ares-emulator/ares) and compiles.
It gives the cartridge use and the CART_IN switch (sections 2–4). Section 5 says what a frontend adds for CD32X
discs.

**The model:** the Mega EverDrive PRO's extended SSF mapper with its write bit set. That makes the EverDrive
16 MB of RAM, in 32 banks of 512 KB, any of which can be switched into the eight 512 KB windows of the 4 MB
cartridge space. That is exactly what was tested on the console ([docs/10](10_EVERDRIVE_TEST.md)).

| Use | How the cart starts | What it is for |
|---|---|---|
| **Cartridge** | A ROM with `SEGA SSF` at `0x100`, at the start of the 16 MB; CART_IN asserted; W = 0 | `edtest`, and any 32X or Genesis program written for the EverDrive |
| **RAM cart of a CD32X disc** | As the launcher `EDBOOT.BIN` leaves it: W = 1, C = 1 (CART_IN released), banks 0–7 in windows 0–7 | Every CD32X disc (OpenLara, BootCamp, ...): they see a 4 MB RAM cart, and can switch banks for more |
| **Launcher first** | Like the cartridge use, with `EDBOOT.BIN` in bank 0: it sets the mapper, releases CART_IN, starts the Sega CD BIOS | Testing the real console's path end to end |

## 1. What is known, and what is a choice

| Fact | How sure | Where from |
|---|---|---|
| The mapper is on for a ROM whose header says `SEGA SSF`, started from the EverDrive menu | **Console** | `edtest`, docs/10 |
| CTRL0 = `0xA000` (P + W) or `0xE000` (P + X + W), written by the 68000 as a word, with RV = 1 | **Console** | `edtest` |
| CTRL1–7 put a 512 KB bank into windows 1–7 (cart `0x080000`–`0x3FFFFF`); banks 1–27 | **Console** | `edtest` |
| The 68000 writes the banks with RV = 1; each keeps its data while the other 26 are switched in and out | **Console** | `edtest`, 3 passes, 0 bad words |
| Both SH2s read the same data at `0x22000000` + the window's offset (cache-through) | **Console** | `edtest` |
| The X bit (32X mode) changes nothing that was measured | **Console** | `edtest` W and X ROMs |
| Every CTRL0 write needs P (bit 15) | krikzz's notes; every console write had it | — |
| Banks 28–31 are reserved: 28 save area (its first 32 KB is copied to the SD card), 30–31 the EverDrive's OS, 31 the PRO's save RAM (512 KB) | krikzz's notes; R. Hargreaves' EverDrive SRAM write-up | not tested |
| CTRL0's R field (the bank in window 0) | not tested: always 0 | **choice:** it works like CTRL1–7 |
| C bit = CART_IN control | krikzz's notes ("#CART signal control") | **not tested**; `EDBOOT.BIN` is the test. **Choice:** C = 1 releases CART_IN |
| Byte writes to the registers | not tested | **choice:** CTRL1–7 take a word write or a byte write to the odd address; CTRL0 takes any write with P set on the data bus |
| W = 0 protects the memory | not tested | **choice:** writes are ignored |
| State after a console reset | not tested | **choice:** windows 0–7 = banks 0–7; W and C as at the start (see section 2) |
| The memory keeps its contents without power | probably not (PSRAM); only bank 31 goes to the SD card | **choice:** the RAM cart use keeps all 16 MB in a file, like the battery-backed 4 MB cart |

A pass in ares means "the code is right", not "the console will do it". Section 8 lists what only the console
can settle.

## 2. The board

**Memory:** 16 MB = 8 M words, bank *n* = words `n << 18` … `(n << 18) + 0x3FFFF`. Fill a new cart with `0xFFFF`.

**Registers** (write-only, in `/TIME` space, so they are reached through `writeIO`, not `write`):

| Address | Register | Bits |
|---|---|---|
| `0xA130F0` | CTRL0 | `P X W L C . . . . . . R R R R R`: taken only when P (bit 15) = 1. X 32X mode (store it, it does nothing), W write enable, L LED, C CART_IN control (1 = released), R the bank in window 0 |
| `0xA130F2` … `0xA130FE` | CTRL1 … CTRL7 | bits 0–4: the bank in window 1 … 7 (window *n* = cart `n × 0x80000`) |

Register number = `(address >> 1) & 7`. Reads of `0xA130Fx` return the bus (the registers are write-only).

**The P bit is a filter, and it matters.** Sega's own mapper uses a byte write to `0xA130F1` to switch save RAM
(`move.b #3,0xA130F1` is in Chilly Willy's 32X start-up code). On the EverDrive that is CTRL0's low byte. A
68000 byte write carries its byte on both halves of the data bus, so bit 15 is that byte's bit 7: the usual
values (0–3) have P = 0 and are ignored. ares's 68000 does the same duplication
(`component/processor/m68000/memory.cpp`: `data << 8 | (n8)data`), so check `data.bit(15)` as it arrives.

**Address decoding.** The cart sees A1–A21 only. Every access arrives at the board with a different
high part:

```
local  = address & 0x3FFFFF          (n22: drop A22-A23)
window = local >> 19                 (0-7)
phys   = bank[window] << 19 | (local & 0x7FFFF)
word   = phys >> 1
```

**Writes:** only while W = 1; `upper` writes byte 1 and `lower` byte 0 of the word. Nothing else writes the cart:
the SH2s cannot (ares has no SH2 write path to `0x02000000`, and the console doesn't either: CD32X stacks let
the 68000 do every cart write).

**CART_IN (C).** C = 1 must make the console see no cartridge *at once*: on the next 68000 access the Sega CD
BIOS is at `0x000000` and the cart is at `0x400000`. A launcher runs this switch from work RAM, because its
own ROM disappears from `0x000000`.

**Power and reset:**

| Use | Power-on | Reset |
|---|---|---|
| Cartridge | banks 0–7 in windows 0–7, W = 0, C = 0, X = L = 0; the ROM loaded at the start of the memory | the same registers; the memory as it is |
| RAM cart (CD32X) | banks 0–7, **W = 1, C = 1** (as EDBOOT leaves it); the memory from the cart file | the same; W and C stay 1 (there is no launcher to run again) |
| Launcher first | as the cartridge; the memory from the cart file, then the launcher ROM over bank 0 | as the cartridge, and the launcher is put back in bank 0 (as starting it again from the EverDrive menu would) |

**Save RAM:** in the cartridge use, bank 31 is the game's save RAM: load it from and save it to a 512 KB `.ram`
file. In the RAM cart use the whole 16 MB is the file.

**Save states:** the 16 MB, the eight banks, X, W, L, C. After loading a state, copy C into wherever the bus
reads CART_IN from (section 4, `cartOff`).

## 3. Where the cart is reached, and which ares code routes it

| Who | Address | When | ares v148 path |
|---|---|---|---|
| 68000 | `0x000000`–`0x3FFFFF` | cartridge boot (CART_IN asserted), 32X off (ADEN = 0) | `md/bus/inline.hpp` `Bus::read` → `cartridge.read` → (32X) `Board::Mega32X::read` → the board |
| 68000 | `0x000000`–`0x3FFFFF` | 32X on (ADEN = 1) | `Mega32X::read`: `0x000000`–`0x0000FF` are the 32X's vectors; the rest reaches the board **only while RV = 1** (`m32x.dreq.vram`) |
| 68000 | `0x880000`–`0x8FFFFF` | ADEN = 1, RV = 0 | `md/m32x/bus-external.cpp`: cart `0x000000`–`0x07FFFF`, reads only in ares |
| 68000 | `0x900000`–`0x9FFFFF` | ADEN = 1, RV = 0 | cart `(0xA15104 & 3) × 1 MB` + offset, read and write |
| 68000 | `0x400000`–`0x7FFFFF` | Sega CD boot (CART_IN released) | `Bus::read` sends it to `cartridge.read` with the address still `0x4xxxxx`: the board must drop A22 |
| SH2 | `0x02000000` (cached), `0x22000000` (cache-through) | ADEN = 1; stalls while RV = 1 | `md/m32x/bus-internal.cpp` `readInternal` → `cartridge.child->read` with the SH2 address cut to 24 bits |
| 68000 | `0xA130F0`–`0xA130FF` | always | `Bus` → `cartridge.writeIO` → `Mega32X::writeIO` → the board's `writeIO`, then `m32x.writeExternalIO` |

`cartridge.child` is the inner board (the 32X wrapper sets it); both the 68000's 32X windows and the SH2s go
through it.

## 4. The ares changes, file by file (v148)

All of this is in the patch.

1. **`ares/md/cartridge/board/everdrive-pro.cpp`** (new): `struct EverDrivePro : Interface` with `ram`,
   `bank[8]`, the four flags, `load` / `save` / `read` / `write` / `readIO` / `writeIO` / `power` / `serialize`.
   Add it to `board/board.cpp` (`#include`) and to `ares/md/CMakeLists.txt`.
2. **`ares/md/cartridge/cartridge.hpp`:** a field `n1 cartOff`, and
   `bootable()` = `information.bootable && !cartOff`. `bootable()` *is* CART_IN. Everything that asks for it then
   follows the C bit: the 68000 map (`bus/inline.hpp`, four places), the 32X's CART bit, the title.
3. **`ares/md/cartridge/cartridge.cpp`:** pick `EverDrivePro` when the pak has `everdrive` or
   `board == "EVERDRIVE-PRO"`; set `cartOff = 0` in `connect()` and `disconnect()`.
4. **`ares/md/cartridge/board/mega-32x.cpp`:** a 32X pak with `everdrive` gets `EverDrivePro` as its inner board.
5. **`ares/md/m32x/io-internal.cpp`:** the SH2's CART bit (`0x4000` bit 8) =
   `!(pak && cartridge.bootable())` instead of `!pak`. Without it, a cart that released CART_IN still looks
   present to the 32X boot ROMs, and they never take the CD path (`_CD_`).
6. **`mia/medium/mega-drive.cpp`, `mega-32x.cpp`:** `SEGA SSF` at `0x100` → the EverDrive board, and a 512 KB
   word-wide save RAM (bank 31) instead of whatever the header's SRAM entry says.
7. **The SH2 recompiler:** when a window changes bank, or the 68000 writes the cart, drop the compiled blocks for
   those addresses in both mirrors (`0x02xxxxxx` and `0x22xxxxxx`) on both SH2s:
   `m32x.shm/shs.recompiler.invalidate(address, size)`, one 256-byte pool at a time, size at most 255 (it is a
   `u8`). The cached mirror is dropped anyway when a game purges its cache (`Cache::purge` →
   `invalidateCached`), but the cache-through mirror never is. Only do this at run time: the board's `power()`
   runs before the SH2s power up and allocate the recompiler's pools.

## 5. For CD32X discs: a cart in the slot while the disc boots

Upstream ares's *Mega CD 32X* system boots a disc with an **empty** cartridge slot, and every CD32X boot stack
needs a RAM cart: without one the disc stops at its boot picture. A frontend that wants the EverDrive as that
cart does this (the CD32X ares build does it for its other RAM carts too):

1. Make a pak (`vfs::directory`) for the cartridge port: `title`, `region` (all three), `bootable = false`
   (CART_IN released: the disc boots), `mega32x = true`, `ramcart = true`, `everdrive = true`, and a 16 MB
   file `ram.cart`.
2. Return it from `Platform::pak()` for the node `Mega Drive Cartridge`, and connect the `Cartridge Slot` port
   when the system loads.
3. After the run, write `ram.cart` back to disk: it holds the game, its data and its saves, like the real
   battery-backed cart.
4. **Launcher first:** the same pak with `bootable = true` and `program.rom` = `EDBOOT.BIN`. The console boots
   the launcher, which releases CART_IN; the board's `cartOff` flips `bootable()`, and the Sega CD BIOS boots the
   disc. (The CD32X ares build's `ares-headless`: `--ramcart-type everdrive --ramcart-boot EDBOOT.BIN`.)

## 6. Mistakes that are easy to make

| Mistake | What you see |
|---|---|
| Decoding the full address instead of A1–A21 | Mode-2 accesses (`0x400000`) and SH2 reads miss the memory: `SAFE CART: NOT FOUND`, or garbage |
| Treating the registers as memory (`write`) instead of `/TIME` (`writeIO`) | The mapper never switches: `edtest` says `MAPPER: NO CHANGE AFTER CTRL1 WRITE` |
| Ignoring P | A game's `move.b #1,0xA130F1` (save RAM switch) moves window 0 away from the running program |
| Requiring P on CTRL1–7 | Banks never switch (P is only in CTRL0) |
| Keeping the ROM read-only (ares's `Standard` board) | `edtest`: `W` for every bank, `NOT WRITABLE WITH THIS CTRL0`. That is what ares v148 shows without this change |
| A cart pak whose CART bit stays "connected" | The 32X boot ROMs skip the CD path; a CD32X disc stops at its boot picture |
| CART_IN cached at load time | The launcher's C bit does nothing; EDBOOT paints the screen red |
| Touching `m32x` in `power()` | A crash at power-on (the recompiler's pools do not exist yet) |
| Not dropping recompiled blocks on a bank switch | Code run through `0x22xxxxxx` keeps running the old bank's code |
| Forgetting `cartOff` after loading a state | A state saved after the launcher ran comes back with the cartridge "inserted" again |
| Letting the SH2s write the cart | Programs that work in ares fail on the console |
| Letting the cart answer the 68000 while ADEN = 1 and RV = 0 (outside the 32X windows) | Programs that work in ares fail on the console (the 32X cuts the cart off) |

## 7. The tests: what "done" looks like

Build the test programs with `bash edtest/build.sh` and `bash edboot/build.sh`. With the CD32X ares build's
`ares-headless` (BIOS file: your own US Sega CD BIOS):

| Test | Command (shortened) | Expected |
|---|---|---|
| Cartridge, write bit | `--game edtest/EDTEST_W.32X --machine 32x --frames 16000` | `MAPPER: SWITCHES BANKS`, every bank `OK`, `BAD WORDS 68K 0 SH2 0`, `ALL TESTED BANKS OK` after 2 passes. That is also the console's result ([photo](../edtest/results/EDTEST_W_A000_pass3.jpg)) |
| Cartridge, X bit | the same with `EDTEST_X.32X` | the same ([photo](../edtest/results/EDTEST_X_E000_pass2.jpg)) |
| Without the change | either ROM in ares v148 | `W` for every bank, `NOT WRITABLE WITH THIS CTRL0` |
| RAM cart | `--game disc/BootCampDiag.cue --machine cd32x --ramcart ed.ram --ramcart-type everdrive --frames 1800` | the green text: `WROTE 5AA5 VIA 900000:5AA5`, `SAFE CART: FOUND`, `STATE:4B10` |
| Launcher first | the same plus `--ramcart-boot edboot/EDBOOT.BIN` | the same, with `OLD:00FF` (the launcher's first word, still in bank 0) |
| The example game | `--game disc/BootCamp.cue ...` as the RAM cart | the hello screen: `LEVEL00.BIN: LOADED FROM THE CD`, `BOOTS 1`, `SAVED`; a second run shows `BOOTS 2` |
| Save states | `--save-state 1400:st.bst`, then a run with `--load-state st.bst` | the game goes on from the state |
| Nothing else broke | the other RAM carts (`--ramcart-size 16`); an ordinary 32X game and a Genesis game | as before |

The CD32X ares build passes all of these (25 Oct 2026), except that no ordinary commercial game was run: the
16MB-PCB cart still boots the diagnostic disc to `4B10`. Ordinary carts don't reach the new code: only ROMs with
`SEGA SSF` and paks marked `everdrive` get the new board, and `bootable()` only changes for a board that sets
`cartOff`.

## 8. What only the console can settle

1. **The C bit.** Does C = 1 release CART_IN, and does the EverDrive still answer at `0x400000` afterwards with
   its SSF mapper and the write bit? `EDBOOT.BIN` is the test: the Sega CD's boot screen, then the disc's,
   means yes; a red screen means C did not release the cartridge (docs/10).
2. **Bank 0 and window 0** (CTRL0's R field), and **banks 28–31**.
3. **What a console reset does** to the registers, and whether the memory keeps its contents through a reset
   and through a power-off.
4. **Byte writes** to CTRL1–7.

Update this document and the board when a console answers one of them.

**Answered on the console, 25 Oct 2026** (and in the ares CD32X board, branch `everdrive-pro`):
- The C bit changes nothing: the EverDrive stays the cartridge at `0x000000` (docs/12). The board's "RAM
  cart" use (C = 1, the cart at `0x400000`) is kept for discs that boot the normal way with another cart.
- Bank 31 is the EverDrive's battery RAM, kept on the SD card as `bram.srm` (256 KB written) when the ROM's
  header declares save RAM; banks 0–29 (PSRAM) are wiped every time the EverDrive menu runs (`edsave/`).
  The board already kept bank 31 as the save file; the wipe is not modelled (ares keeps the 16 MB).
- Two rules the board now models, both from `edw0` and the cartridge boot: with W = 1, a 68000 write to
  anything but the cart disturbs an SH2 cart read within 1 µs of it (the read returns 0 and the word takes
  the 68000's data - deterministic, not exact); and a direct 68000 write to window 0 (`0x000100`–
  `0x07FFFF`) with the 32X on is dropped. `Bus::write` reports the non-cart writes through a new board hook
  (`disturb`); the `/TIME` registers are excluded (changing W is harmless). Regression: `EDCD_H3` boots
  BootCamp and OpenLara, `EDCD_DIAG_H2` (W always on) fails with `4DE2`.
- Still open: byte writes to CTRL1–7, a console reset's effect on the registers.
