# 06 – The EverDrive in ares

The ares CD32X build (ares v148 + the RAM carts) models the Mega EverDrive PRO as a cartridge board,
`ares/everdrive-pro.cpp`. The CD32X BootCamp's `docs/11_ARES_16MB_RAM_CART.md`
(`bootcamp-docs/11_ARES_16MB_RAM_CART.md`) is the design document: what is known, what is a choice,
and the mistakes that are easy to make. `ares/README-CD32X.md` is the user's manual, and
`ares/everdrive-pro-ares-v148.patch` is the whole change against upstream ares v148 (the board, the
RAM carts, mia, the desktop and headless front ends).

## The board

- 16 MB as 32 banks of 512 KB, eight windows, the registers as in docs 01 (P must be set; CTRL0 sets
  X, W, L, C and window 0's bank; CTRL1-7 from word writes and odd-byte writes).
- The RAM starts with a fixed pattern (`address * 0x9E37 + 0x5A5A`), not blank, so a program that
  reads what it never wrote shows it (a blank `0xFF` hid the cartridge boot's Word RAM bug).
- Three uses, by pak attributes: a `SEGA SSF` cartridge (bank 31 = the `.ram` save file); the RAM
  cart of a CD32X disc (W = 1, C = 1, as a launcher leaves it, all 16 MB in the cart file); and the
  **launcher** (`program.rom` bootable from bank 0, W = 0, C = 0 as the EverDrive menu starts it; a
  reset puts the launcher back in bank 0).
- SH2 recompiler: a bank switch or a cart write invalidates the compiled code of that window.

## What the console settled, and the model follows (25 Oct 2026)

| Console | ares |
|---|---|
| The C bit changes nothing; the EverDrive stays the cartridge | the launcher use: `cartOff` stays 0, the disc boots from the cart |
| Mapper writes reach the cart only with RV = 1 (32X on) | `mega-32x.cpp` `writeIO`: `/TIME` writes pass to the board only with RV = 1 |
| Window 0 cannot be written by the 68000 directly | `mega-32x.cpp` `write`: a 68000 write to `0x000100`-`0x07FFFF` with RV = 1 is dropped |
| With W = 1, a 68000 write elsewhere disturbs an SH2's cart read (zeros, a changed word) | `Bus::write` reports every non-cart 68000 write to the board (`disturb`); an SH2 cart read within 1 µs of one returns 0 and the word takes the 68000's data. `/TIME` writes excluded (changing W is harmless); writes made with RV = 1 excluded (the 32X holds the SH2s off the cart then). Deterministic, not exact |
| Copies from Word RAM with RV = 1 come out garbled | `Bus::read` of `0x400000`-`0x7FFFFF` with the cart bootable, the 32X on and RV = 1: the Mega CD's data AND the cart's (two drivers on the bus) |
| Bank 31 goes to the SD card | the `.ram` save file (cartridge use) / inside the cart file (launcher use) |
| The PSRAM is wiped by the menu | **not modelled**: the cart file keeps the 16 MB (a choice - it is also the battery-backed 16MB-PCB cart's behaviour) |
| A 68000 copy during the slave's PWM DMA read locks the SH2s | **not modelled**: ares ran the unparked saves fine |

Regression, in `ares-headless`: the launcher that works on the console (`EDCD H3`) boots BootCamp
and OpenLara; the earlier one with W always on (`EDCD H2`) fails with `4DE2`, as it did on the
console; a launcher that writes window 0 directly fails too.

## Running the cartridge boot

Windows (v1.2 and later): Settings → Options → cart type **Mega EverDrive PRO**, **EverDrive
launcher → Browse** to `EDCD.32X`, then Load the disc. Headless:

```bash
ares-headless --game disc.cue --machine cd32x --bios-us bios_CD_U.bin --home ./home \
    --ramcart cart.bin --ramcart-type everdrive --ramcart-boot EDCD.32X --frames 4400
```

A plain probe ROM (`EDTEST`, `EDW0E`, `EDSAVE2`): `ares-headless --machine 32x --game X.32X`. mia
picks the EverDrive board for a `SEGA SSF` header **without** a save-RAM entry; `EDSAVE1` carries
one (the console needs it for `bram.srm`), so build it with `ED_SRAM=0` to run it in ares.

## Where things are

| File | What |
|---|---|
| `ares/everdrive-pro.cpp` | the board |
| `ares/mega-32x.cpp` | the 32X cartridge wrapper: RV gating of reads, writes and `/TIME`, the window-0 rule |
| `ares/inline.hpp` | the 68000 bus: the `disturb` hook, the two-drivers read |
| `ares/everdrive-pro-ares-v148.patch` | everything, against upstream v148 |
| `ares/README-CD32X.md`, `ares/RELEASE_NOTES_v1.1.md` | the manual, the release notes |
