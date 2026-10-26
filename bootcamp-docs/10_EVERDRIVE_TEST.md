# 10 – Mega EverDrive PRO as a 16 MB RAM cart: the test

**The question:** can a CD32X game use the EverDrive PRO's 16 MB of memory instead of the 4 MB RAM cart?

**What is known.** Booting the diagnostic disc through the EverDrive's "Run Mega-CD" (Sega CD + 32X, EverDrive
in the 32X slot) found **no writable memory** at the cart:

- `OLD:0000 VIA 400000:FFFF`
- `WROTE 5AA5 VIA 900000:0000`
- `SAFE CART: NOT FOUND`
- the first ROM chunk did not stick (`CMP I0 C0 @400000 BAD`)

So in that mode the EverDrive is not a RAM cart. (Its manual says why: "Run Mega-CD" releases the cartridge and
starts the external Sega CD with the EverDrive's *backup RAM* cart function on, not its SSF RAM.)

**What this tests.** The EverDrive's **extended SSF mapper**. Per Krikzz's notes, it can make its memory
writable and has a 32X mode, but it only turns on for a ROM whose header says `SEGA SSF`, started from the
EverDrive menu.

| Register | Bits | Use |
|---|---|---|
| `0xA130F0` CTRL0 | `P X W L C . . . . . . R R R R R` | P protection (must be set), X 32X mode, W write enable, L LED, C #CART (Pro), R the bank in window 0 |
| `0xA130F2`–`0xA130FE` CTRL1–7 | `. . . . . . . . . . . R R R R R` | the bank in 512 KB windows 1–7 (cart `0x080000`–`0x3FFFFF`) |

Banks are 512 KB each, 0–31. Banks 28 (saves) and 30–31 (the EverDrive OS; 31 is SRAM on the Pro) must not
be touched.

## Results on the console (24 Oct 2026)

Genesis + 32X + Mega EverDrive PRO, started from the EverDrive menu.

| ROM | CTRL0 | Result | Photo |
|---|---|---|---|
| `EDTEST_W.32X` | `0xA000` | 2 full passes (the second with every bit inverted), pass 3 under way at 226 s: all 27 banks `OK`, 0 bad words from the 68000 and from both SH2s | [`edtest/results/EDTEST_W_A000_pass3.jpg`](../edtest/results/EDTEST_W_A000_pass3.jpg) |
| `EDTEST_X.32X` | `0xE000` | 1 full pass, pass 2 under way: all 27 banks `OK`, 0 bad words | [`edtest/results/EDTEST_X_E000_pass2.jpg`](../edtest/results/EDTEST_X_E000_pass2.jpg) |

**Which build:** the photos show the build of 24 Oct, 23:16 (commit `fdf5e0e`). It has no `MAPPER` or `RUN`
line; it writes and checks every bank exactly as described below.

The build sent at 23:22 (`09204ba`, the review's additions: the run number, the mapper check) had a bug of
ours. `ed_setup` fell through into its own error exit, so every fill and every 68000 check returned at once
without touching the cart; on a console it would have shown `S` for every bank. It was fixed on 25 Oct (a
missing `bra.b ed_quarter`). In ares, the fixed ROMs give the console's result, and the 23:22 build did no work.

**Verdict: `ALL TESTED BANKS OK` with both values.** In SSF mode the EverDrive PRO is 16 MB of writable RAM
for a 32X program:

- the 68000 writes it with RV = 1;
- the mapper switches banks with the 32X on;
- the SH2s read back exactly what the 68000 wrote.

**Earlier attempts, for the record:**
- **"Run Mega-CD" with the diagnostic disc:** no writable memory at the cart (`SAFE CART: NOT FOUND`).
- **The first build of this test** stopped with `68000 NOT ANSWERING`. A bug in the test: the 68000's vertical
  blank handler clobbered `d0`/`a0` and wrote the pads into COMM8/10. It was fixed by keeping the 68000's
  interrupts off for the whole test.

**Next:**
1. **Done in ares, next on the console:** the EverDrive as the CD stack's RAM cart, with `EDBOOT.BIN`
   ([below](#edbootbin-the-everdrive-as-the-ram-cart-of-a-cd32x-disc)).
2. **Done:** the ares cart type, [docs/11](11_ARES_16MB_RAM_CART.md). In it, both ROMs give the console's
   result: 2 full passes, all banks `OK`, 0 bad words.
3. The games' paging command (CMD 68) in an EverDrive form.

## EDBOOT.BIN: the EverDrive as the RAM cart of a CD32X disc

> **Result on the console (25 Oct 2026): the C bit does nothing.** `edboot/EDPROBE.BIN` writes CTRL0 = `0xA800`
> once, as EDBOOT does, then shows the header text and reset PC at `0x000000` and `0x400000` and looks for
> signatures it wrote into the eight windows. Right after the write, one second later, after a 68000 RESET and
> after a second write: `000100 SEGA SSF` (still the EverDrive, PC `0200`), `400100 SEGA MEGA DRIVE` (still
> the Sega CD BIOS, PC `0426`), all eight signatures at `0x0xxxxx`, none at `0x4xxxxx`, writes stick only
> low. The console stays in cartridge mode. (EDBOOTD's earlier "CART OFF" was wrong: its check compared only
> "SEGA", which the EverDrive's own header has too.) EDBOOT was never run on the console: the disc tests of
> 24 Oct were started with "Run Mega-CD".
>
> **What works instead:** the disc started from the cartridge, `edboot/EDCD.32X` ([docs/12](12_CARTRIDGE_BOOT.md)).

A CD32X disc boots from the Sega CD, with **no** cartridge seen by the console (CART_IN released), and its stack
copies the game into the RAM cart at `0x400000`. `edboot/EDBOOT.BIN` (32 KB, header `SEGA SSF`) sets the
EverDrive up as that cart:

1. Checks that a Sega CD is there (its BIOS at `0x400000` while a cartridge runs).
2. Sets the mapper as a 4 MB RAM cart: banks 0–7 in windows 0–7, then CTRL0 = `0xA000` (P + W).
3. From work RAM, sets CTRL0's C bit (`0xA800`): CART_IN is released, so the Sega CD BIOS is at `0x000000` and
   the EverDrive's memory at `0x400000`.
4. Checks that the BIOS really is at `0x000000`, and starts it as a reset would: the BIOS boots the disc.

Nothing in the disc or its boot stack changes: the stack finds a 4 MB RAM cart (16 MB behind the mapper).

**How to run it:** Genesis + Sega CD + 32X, the EverDrive in the 32X slot, the CD32X disc (burned) in the Sega
CD. Copy `EDBOOT.BIN` to the SD card and start it from the EverDrive menu.

| What you see | Meaning |
|---|---|
| The Sega CD logo, then the disc's boot picture, then the game | It works: the C bit releases CART_IN and the EverDrive is the RAM cart |
| A **red** screen | C = 1 did not release the cartridge (the BIOS did not appear at `0x000000`); CART_IN is back on |
| A **blue** screen | no Sega CD found |
| The Sega CD logo, then the disc stops at its boot picture | the BIOS booted, but the stack found no RAM cart: the EverDrive does not answer at `0x400000` after C. Try the diagnostic disc (`DIAG=1 bash build.sh`): its `SAFE CART` line tells which |

**The memory is not battery-backed.** The EverDrive's memory is very likely lost at power-off (only bank 31, its
save RAM, goes to the SD card). A game's saves in the RAM cart survive a reset, not a power-off.

**In ares** (docs/11): `ares-headless --ramcart-type everdrive --ramcart-boot edboot/EDBOOT.BIN` with the
diagnostic disc gives `SAFE CART: FOUND` and `STATE:4B10`. Build it with `bash edboot/build.sh`.

## The two ROMs

| ROM | CTRL0 | From |
|---|---|---|
| `EDTEST_W.32X` (run first) | `0xA000` = P + W | the Pro write-up, which shows no X bit: the documented value for the Pro |
| `EDTEST_X.32X` | `0xE000` = P + X + W | the X-series notes, which list an X (32X) bit |

Build them with `bash edtest/build.sh` (`ROOTDIR=` / `SEGA_TOOLCHAIN=` as for the main build).

## How to run it

1. Copy both `.32X` files to the EverDrive's SD card.
2. Genesis + **32X** with the EverDrive PRO in the 32X slot. The Sega CD is not needed: take it off, or leave
   it, it is not used.
3. Start `EDTEST_W.32X` from the EverDrive menu. Leave it for at least one full pass (`PASS DONE`), better two
   or three. Photograph the screen.
4. **Power off** (not reset), then do the same with `EDTEST_X.32X`.

Each run mixes its own number (`RUN xxxx` on screen) into the patterns, so data an earlier run left in the
EverDrive's RAM can never pass for this run's writes.

## What it does

Each pass:

1. Writes EverDrive banks 1–27 (13.5 MB) with the 68000, with RV = 1, as the CD32X stack writes its RAM cart.
   Each bank gets its own pattern, mapped seven at a time into windows 1–7. Each bank is checked right after
   it is written.
2. After **all** banks are written, maps each one in again and checks it three ways: the 68000, the master
   SH2 (first half) and the slave SH2 (second half). The SH2s read cache-through at `0x22000000`, as a game
   reads its cart.

Every other pass the pattern's bits are inverted. Bank 0 (the test program itself) and banks 28–31 are never
written.

## Reading the screen

| Per bank | Meaning |
|---|---|
| `OK` | written, kept, and read back the same by the 68000 and both SH2s |
| `W` | not writable, or not mapped: it did not read back right after writing |
| `A` | written, but lost or overwritten after the other banks were written: the banks share memory, or the mapper does not switch |
| `S` | the 68000 reads it right, an SH2 does not |

- **`MAPPER`:** checked before any bank is written. Bank 0 (this ROM) is mapped into window 1, and then bank 1.
  `SWITCHES BANKS` = the mapper responds. `DOES NOT RESPOND` or `NO CHANGE AFTER CTRL1 WRITE` = the
  EverDrive did not turn its SSF mapper on for this ROM, or the register writes do not reach it through the
  32X. So any `W` then means "no mapper", not "no RAM".
- **`BAD WORDS 68K / SH2`:** totals over all passes.
- **`FIRST ERROR`:** the bank, the offset in it, the word expected, the word read, and who read it.
- **The verdict line** sums it up.

| Verdict | What it means for the games |
|---|---|
| `ALL TESTED BANKS OK` (after several passes, in one of the ROMs) | the EverDrive can be a 16 MB cart for a 32X program in **cartridge mode**. Next: boot the CD stack from it (a separate step, since the CD boot needs the cart to step aside). |
| `NOT WRITABLE WITH THIS CTRL0` | with `MAPPER: SWITCHES BANKS`: that CTRL0 value does not enable writing with the 32X; try the other ROM. Without it: see `MAPPER`. |
| `BANKS LOSE DATA OR ALIAS` | the writes land, but the mapper's banks do not switch under the 32X. The `GOT` word tells which bank's pattern was found. |
| `SH2 READS DIFFER FROM 68000` | the memory works, but the SH2s see something else: timing or the 32X's cart path |
| `68000 NOT ANSWERING` | the 68000 side stopped. The screen shows the command that did not come back, its argument and the eight COMM registers. (A 68000 stuck while it holds the cart, RV = 1, freezes the screen instead: the SH2s wait for the cart.) |

In ares v148 this ROM shows `MAPPER: SWITCHES BANKS` and `W` for every bank: plain ares emulates the standard
SSF mapper (bank switching, read only), not the EverDrive's write bit. With the EverDrive board
([docs/11](11_ARES_16MB_RAM_CART.md)) it shows the console's result.

## Afterwards: `edw0/` (25 Oct 2026)

edtest proved the memory with the 68000 idle between its commands. The cartridge boot then showed what
edtest could not: with the EverDrive's write-enable bit on, a 68000 **write to any other address** (work RAM,
the VDP, the gate array) can land in the cart and change it, and the SH2's read of the cart at that moment
comes back as zeros. `edw0/` (built from edtest: `bash edw0/build.sh`) is the probe that measured it - the SH2
reads window 0 while the 68000 does one thing at a time, under the write-enable bit on and off. The result
and the rule it gave the listener are in [docs/12](12_CARTRIDGE_BOOT.md). `EDW0E_W.32X` is the one to run.
