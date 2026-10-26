# 01 – The Mega EverDrive PRO as 16 MB of RAM

## What the EverDrive is, as a cartridge

The Mega EverDrive PRO holds 16 MB of PSRAM (two 8 MB chips) for the game it loads from its SD card,
512 KB of battery-backed memory for saves, and an FPGA that maps them into the Genesis cartridge
space with one of several "mappers". For a ROM whose header says `SEGA SSF` (offset `0x100`), started
from the EverDrive menu, it uses its **extended SSF mapper**: the 4 MB of cartridge space becomes
eight 512 KB windows, each showing one of 32 banks of 512 KB, and the memory is **writable** when its
write bit is on. krikzz published the mapper's source (github.com/krikzz/MEGA-PRO,
`fpga/mapper/map_ssf/map_ssf.sv`); the facts below were measured on a console before that source was
read and agree with it.

## The registers (write-only, in the cartridge's /TIME space)

| Register | Bits | Meaning |
|---|---|---|
| `0xA130F0` CTRL0 | `P X W L C . . . . . . R R R R R` | taken only when **P** (bit 15) is 1. **X** (14) "32X mode" - no effect seen. **W** (13) write enable. **L** (12) the LED. **C** (11) #CART - no effect on a 32X console (the EverDrive stays the cartridge). **R** (0-4) the bank shown in window 0 |
| `0xA130F2`-`0xA130FE` CTRL1-7 | bits 0-4 | the bank shown in window 1-7 (cart `0x080000`-`0x3FFFFF`) |

A byte write to `0xA130F1` (Sega's own SRAM switch) carries its byte on D8-D15 as well, so P is that
byte's bit 7 and ordinary SRAM-switch values are ignored. In the mapper source the register write
needs the lower byte strobe (`!cpu.we_lo`): a word write or an odd-byte write.

With the 32X on, the 68000's register writes reach the cartridge **only with RV = 1** (the 32X's
`0xA15107` bit 0, "ROM to VDP" - it hands the cartridge bus to the 68000). Writes made with RV = 0
left the EverDrive write-protected.

## The banks

| Banks | What | Lifetime |
|---|---|---|
| 0-15 | PSRAM chip 1 (`mem_ce == 0`) | **wiped by the EverDrive OS every time its menu runs** (measured: `edsave`, three round trips, 25 Oct 2026). Bank 0 gets the launched ROM. |
| 16-30 | PSRAM chip 2 (`mem_ce == 1`); 28-30 are used by the EverDrive's own software (its menu lives at `0xF80000` = bank 31 of that space, its data buffer at bank 30) | the same wipe. Leave 28-30 alone. |
| 31 | **the backup memory** (`mem_ce == 2`): battery-backed, and written to the SD card by the OS as `MEGA/gamedata/<ROM name>/bram.srm` when the ROM's header declares save RAM (an `RA` entry at `0x1B0`) | kept across the menu, power-off and (through the file) card changes. 256 KB written. Its first 256 KB are the game's; above that the OS keeps its own things (the Mega CD core's internal BRAM image, a CD BIOS). |

Reset state: banks 0-7 in windows 0-7, W = 0, C = 0, LED off - as the menu starts a game.

## What the console settled (the three rules of the cartridge boot)

Measured with the probes in `probes/` and the cartridge boot itself (docs 05 has the chronology):

1. **The 68000 cannot write window 0 (`0x000000`-`0x07FFFF`) directly with the 32X on.** Nothing
   landed there whatever was tried (bytes, words, other banks in window 0, a buffer). The 32X's
   adapter answers the 68000 there. A copy into window 0 goes through **window 1** with window 0's
   bank mapped in for the block, then window 1's own bank again.
2. **W must be off whenever the 68000 writes anything but the cart.** With W = 1, some of the 68000's
   writes to work RAM, the VDP or the Sega CD's gate array - what a listener does all the time -
   land in the cart and change its contents, and the SH2's reads of the cart come back as zeros
   meanwhile (`edw0`: perfect with the 68000 idle or *reading* anything, broken as soon as it
   *writes* with W on). Changing W itself does no harm. So W goes on right before a copy into the
   cart and off right after, with RV = 1 (the SH2s held off) and no other 68000 write in between.
   The mapper source shows why: the memory's write strobes follow the console's /LWR and /UWR
   directly whenever the cartridge's chip select is low and `wr_on` is set.
3. **Word RAM and the cart are never touched with the same RV.** Copies from Word RAM made with
   RV = 1 came out garbled in cartridge mode; so every block goes through a 1 KB buffer in work RAM:
   the Sega CD side with RV = 0, the cart side with RV = 1 and interrupts off.

And two more, from the saves (docs 04):

4. **The PSRAM does not outlive the menu.** Saves, fast-load records, a level kept in the cart: all
   gone at the next launch. The saves live in bank 31.
5. **A 68000 copy with RV = 1 while the slave SH2 draws and its PWM mixer's DMA reads the cart
   locks both SH2s.** Park the slave (`MARS_WAIT`) and stop the mixer first, as the level loads do.

## What the SH2s see

The SH2s read the cart at `0x02000000` (cached) and `0x22000000` (cache-through), the same four
windows. Both SH2s read every bank exactly as the 68000 wrote it (`edtest`: 27 banks, three passes,
0 bad words). **While RV = 1, an SH2's access to the cart is NOT held until RV = 0 on the
EverDrive** - it is answered at once, with garbage (`edsave3`, console, 26 Oct 2026: the slave read
965,207 bad words of 978K through the 68000's RV = 1 copies, and got them faster than real reads;
the master 1,309 of its own; reads with RV = 0 and with the 68000 idle: 0). The 32X manual says
only that the SH2 must not access ROM then; a real cart stalls it (D32XR relies on that). So the
rule for a game: **during every 68000 copy with RV = 1, both SH2s must run from SDRAM with their
interrupts masked and touch nothing in the cart** - a cached loop is luck, and in a game the cache
is cold. That is rule 8, and what the passport freeze was.

## What is still a choice

Byte writes to CTRL1-7, the state after a console reset, and whether W = 0 protects every bank
(the probe's pattern was never changed with W off, but it was not tried everywhere).
