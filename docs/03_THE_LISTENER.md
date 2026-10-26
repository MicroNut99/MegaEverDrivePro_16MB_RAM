# 03 – The 68000 listener for the cartridge boot (`MODE1`)

`listener/hw_md.s` is the boot stack's 68000 listener: the program the Sub-CPU drives through the
gate array (commands 0-35: memcpys, 32X register reads and writes, the pad, the VDP, the cart). The
Sub-CPU program is **unchanged** for the cartridge boot; everything the EverDrive demands is in the
listener, under switches that leave the normal build byte-identical (the Sub-CPU program's md5 is
checked by the kit's `tools/check_boot.py` after every change).

## MODE1-1: the map

Built with `make CDROMPlayer.bin MODE1=1` (`OL_MODE1`). Every address argument the listener gets
from the Sub-CPU - which still thinks in CD-boot addresses - is moved: Word RAM `0x2xxxxx` →
`0x6xxxxx`, the cart `0x4xxxxx`-`0x7xxxxx` → `0x0xxxxx`-`0x3xxxxx`. Commands 19-22 (32X register
reads and writes) are left alone: their values are data. The 32X is not reset and gets no CD boot
table (it is running already). The listener's start-up message reads `Init 32X M1`.

## MODE1-1: every copy through a buffer

A copy to or from the cart goes in blocks of up to 1 KB through a buffer in work RAM: the Sega CD
side (Word RAM) read or written with RV = 0, the cart side with RV = 1 and interrupts off (the
68000's vectors are the cart's while RV = 1, so no interrupt may come then; about 5 ms, a vblank
waits at most that long). `m1_xfer` is that routine; `chilly_rv_memcpy`, `safe_cart_xfer` and
`safe_cart_xfer_w` call it in MODE1.

## MODE1-3: window 0 through window 1

A cart address below `0x080000` cannot be written directly (rule 1). `m1_cart` maps window 0's bank
into window 1 (CTRL1 = `OL_M1W0BANK`, 0 unless `EDCD_W0=` says otherwise), copies at
`0x080000 + offset`, and puts bank 1 back. Reads of window 0 go the same way.

## MODE1-6: the write bit

CTRL0 is `OL_M1CTRL0` (`0x8000`, or `0xC000` with `EDCD_X=1`) - the write bit **off**. `m1_cpy_w`
turns it on (`OL_M1CTRL0_W1`), copies, turns it off; it is used only for a copy *into* the cart,
with RV = 1 already set by the caller, and the only 68000 write inside the window besides the cart
is its own `bsr` push (harmless: the SH2s are held off the cart while RV = 1). Reads of the cart
keep W off.

The constant is written `OL_M1CTRL0 + 0x2000 + OL_M1W0BANK`, not with `|`: in m68k GNU as `|`
starts a comment, and the first build silently assembled `0x8000` for both values.

## MODE1-7: the save bank

`M1SAVE=1` (`OL_M1SAVE`, from `EDCD_SAVE=1`): a cart address in `0x800000`-`0x83FFFF` means the
EverDrive's bank 31 - the battery RAM that the OS keeps on the SD card. `m1_cart` maps bank 31 into
window 1 for the block, copies at `0x080000 + (address - 0x800000)`, and puts bank 1 back: the
MODE1-3 route with another bank. Only the bank's first 256 KB; above that the OS keeps its own
things. The Sub-CPU reaches it with `MD_CMD_CHILLY_RV_MEMCPY` to or from `0x800000 + offset`
(`m1_arg` leaves addresses at `0x800000` and above untouched).

`M1SAVEBANK=<n>` / `M1SAVEWIN=<1-7>` (`OL_M1SAVEBANK`, `OL_M1SAVEWIN`; OpenLara's `build.sh`:
`EDCD_SAVEBANK`, `EDCD_SAVEWIN`): another bank for the saves and another window to show it in for
the copies. Defaults 31 and 1. Made for the EDSAVE-3 console tests of the in-game passport freeze
(26 Oct 2026): bank 27 (PSRAM - nothing persists, but it asks whether the battery RAM is what
freezes the game) and window 7 (window 1 holds the Caves level data while it is played; window 7
is free then). Without `M1SAVE` nothing of this assembles.

## MODE1-2, 4, 8: the rest

| Switch | Effect |
|---|---|
| `M1WORDS=1` | copies whose source, destination and length are even go in words |
| `M1MINVB=1` (diagnostics) | the vblank handler does only the Sub-CPU's level 2 interrupt |
| `M1NOVINT=1` (diagnostics) | the vblank handler skips the level 2 interrupt (CD reads stall later: expected) |
| `OL_DISPON` (from `DIAG=1`) | `md_init_hw` leaves the Genesis display on, so a diagnostic launcher's green text shows over the game |
| `OL_M1CTRL0=<v>`, `OL_M1W0BANK=<n>` | the launcher's CTRL0 without W; the bank window 0 shows |

## How a chunk gets into the cart, end to end

1. CDLOADER (SH2): `CMD 46`, chunk *n*.
2. The Sub-CPU reads 32 KB from the CD into Word RAM, gives Word RAM to the 68000, and asks for
   `MD_CMD_CHILLY_RV_MEMCPY (0x400000 + offset, 0x200000, 32768)`.
3. The listener: `m1_arg` turns that into `(0x000000 + offset, 0x600000, 32768)`; `m1_xfer` takes
   1 KB at a time: Word RAM → buffer with RV = 0; then `m1_cart`: if the cart address is below
   `0x080000`, window 1 = bank 0 and the address + `0x080000`; interrupts off, RV = 1, W on, buffer →
   cart, W off, RV = 0, window 1 = bank 1, interrupts on. 32 times.
4. The Sub-CPU reads the chunk back through the 32X's cart window (`0x900000`, RV = 0, bank register
   `0xA15104`) and compares; replies to CDLOADER.
