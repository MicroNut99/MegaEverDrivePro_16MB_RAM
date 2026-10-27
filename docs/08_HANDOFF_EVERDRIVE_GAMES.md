# 08 – Hand-off: building CD32X games for the Mega EverDrive PRO

Written 26 Oct 2026, 09:30, updated 12:55 (ED-SAVE in every game; Doom on S:; Hurrican's high scores found off the path), for the next chat. The person you work with (MicroNut99) owns the
console and the EverDrive, burns the discs and runs the tests; you build and check everything
first. Read this, then `docs/00`–`07` of this repository as needed.

## Their standing rules (keep them, verbatim where quoted)

- **"Do NOT rewrite the boot stack. Ever. Period."** Every change behind a default-off switch;
  the default Sub-CPU program / listener must assemble byte for byte as before (compare the
  binaries, not the sources). The CD32X BootCamp's `tools/check_boot.py` guards its stack
  (`--update` only after a deliberate, switch-guarded change); SuperTux has its own guard
  (`tools/check_boot.py` + `docs/BOOT_FILES.md5`, Hurrican's boot path + approved blocks).
- Only console-proven operations go to the console. No game data, BIOS or disc images in any repo.
  Do not list private repos (Sonic2011CD32X, ares_android, 16MB-PCB-CD32X) as sources.
- Never write the user's personal e-mail address into any document or commit (they asked; only the GitHub noreply author address is used).
- They don't use the Downloads folder: deliver to `S:\KoboPort\sega-toolchain-12.1\sega\<game>`
  through the device tools (30 MB per file; `Makefile`s are "protected" there - edit them in
  place with a python one-liner over the device shell).
- Short answers; answer the question first. "Review all the data before sending me out on a
  crazy test." Send all tests at once, in the order to run them. Every disc and launcher gets a
  **unique, versioned name** (nothing may overwrite the 16 MB-cart build of the same game).
  26 Oct 11:16, their words: "Make sure any new 32X.bin and bin/cue file built has a unique EDCD
  name tag." **A disc names the launcher it belongs with**, because the launcher's file name is the
  save's name (`MEGA/gamedata/<launcher>/bram.srm`), so the disc tells you which save it will use:
  `OpenJazzCD32X_EDCD_OPENJAZZ` + `EDCD_OPENJAZZ.32X`, `HurricanP1q_EDCD_HURRICAN` +
  `EDCD_HURRICAN.32X`, `D32XR_CD32X_R2_EDCD_DOOM` + `EDCD_DOOM.32X` (and Doom's variants
  `..._EDCD_DOOM_SIGIL` / `_ROMMAPS` with launchers to match; a launcher that is *not* the
  EverDrive's gets its own name, `EDCD_DOOM_PAGE7.32X`). Two exceptions, both deliberate:
  **OpenLara** keeps `EDCD_OPENLARA.32X` + `CDROMPlayer_cartED_music` because it is burned, tested
  and its save works; and **SuperTux** keeps `SuperTuxREV11-ED-T1` (MicroNut99, 26 Oct 13:50: "Tux
  is good. No change") - its name is built by a `sed` of the version string in `game/stx.h`.
  The 4 / 8 / 16 MB discs always keep the names they were built under (`_cart8`, `_cart16`).
  Override a disc name with **`DISC_NAME`, never `NAME`**: WSL exports `NAME` as the Windows
  machine's name, and `${NAME:-...}` took it - Doom's first ED disc came out as `discs/Mumra`.
- Build commands in their WSL form:
  `cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/<game>` then
  `... bash /mnt/s/.../build.sh 2>&1 | tee /mnt/s/.../build_edcd_log.txt`.
- Commits: `git -c user.name=Claude -c user.email=noreply@anthropic.com commit --author="MicroNut99 <317976051+MicroNut99@users.noreply.github.com>"`,
  message ends with the Co-Authored-By / Claude-Session lines; never rewrite history. Everything
  committed and pushed.
- No OutRun (Cannonball) and no Diablo (Devilution): they said so.

## Which carts

The Mega EverDrive **PRO** (everything was measured on it) and the **X7** (26 Oct 2026, 10:36: the
user ran `edtest`, the OpenLara launcher + disc and a save through a reset on an X7 - all as on
the PRO, the save kept). Treat "the EverDrive" as both. The new CORE is untested.

## What the EverDrive is, in one paragraph (docs/01)

A `SEGA SSF` ROM turns the EverDrive's extended mapper on: 32 banks of 512 KB in eight windows,
`CTRL0` (`0xA130F0`: P bit, X, W = write enable, L, C, window 0's bank) and `CTRL1..7`
(`0xA130F2..FE`), writable only with RV = 1. Banks 0-30 are PSRAM, **wiped by the menu**;
**bank 31 is battery RAM**, kept on the SD card as `MEGA/gamedata/<launcher>/bram.srm` (first
256 KB; the OS's own above) when the launcher's header has an `RA` entry. The CD boot is
impossible with it in the slot; the disc is booted *from* the cartridge by `EDCD_<game>.32X`,
which carries the disc's Sub-CPU program (built `MODE1=1`) and the SH2 loader (docs/02, 07).
The three listener rules (write bit off except inside a copy; window 0 through window 1; a
buffer between Word RAM and the cart) are the stack's business (docs/03).

## The two rules found this weekend that every game must obey

1. **An SH2 cart access while the 68000 holds RV = 1 is answered with garbage, not held**
   (`edsave3`, docs/05). So for every command whose 68000 side copies with RV = 1 both SH2s
   must wait in a loop that lives in SDRAM with interrupts masked. OpenLara: `csSpin` +
   `MARS_CMD_PARK` in `main.cpp`; the Jazz-shaped games: `sdram_wait` + `JOB->park` in
   `port/plat_mars.c` (`send_cmd_rv`); D32XR already does it (its CMD-IRQ wait and handlers are
   in `.sdata`).
2. **Page 7 is out of bounds** for the paged games (2 MB pages = four banks; page 7 = banks
   28-31). Their EverDrive builds probe and use pages 1-6.

## The listener switches (CD32X BootCamp `stack/subcpu/hw_md.s`, docs/03)

`MODE1=1` the cartridge boot; `M1CTRL0=0x8000`; `M1SAVE=1` the save bank (cart `0x800000`-
`0x83FFFF` = bank 31, `M1SAVEBANK`/`M1SAVEWIN` for tests); **`M1PAGE=1` (MODE1-9)**: the paged
games' one-byte page writes to `0xA130F0/F2` become the EverDrive's window registers (page p in
window 1 = `CTRL4..7` = 4p..4p+3). A game's listener = the stack's file + the game's own
commands merged on top (Jazz 75 lines, SuperTux 49, D32XR 26, Hurrican none); check the
default program is byte-identical after the merge.

## Per game

| Game | Repo (branch main) | Switch | Launcher | Disc | State |
|---|---|---|---|---|---|
| OpenLara REV | OpenLaraCD32X-REV `cart-test-8mb-16mb` (+ mirror OpenLaraCD32X-REV_EDCD) | `CART=ED EDCD=1 EDCD_SAVE=1` | `subcpu/EDCD_OPENLARA.32X` | `subcpu/CDROMPlayer_cartED_music.cue` | **Works on the console**: boots, plays, saves on the SD card (bank 31), in-game save/load/relaunch confirmed 26 Oct 01:02. Reference implementation. |
| OpenJazz REV | OpenJazzCD32X-REV | `CART=ED EDCD=1 EDCD_SAVE=1` | `cd32x/subcpu/EDCD_OPENJAZZ.32X` | `cd32x/subcpu/OpenJazzCD32X_EDCD_OPENJAZZ.cue` | ED-SAVE `243bd20` (CMD 61 to bank 31, CMD 62 piece in). On S:. **Not yet run on the console.** |
| Hurrican | HurricanCD32X | `CART=ED EDCD=1 EDCD_SAVE=1` | `cd32x/subcpu/EDCD_HURRICAN.32X` | `cd32x/subcpu/HurricanP1q_EDCD_HURRICAN.cue` | ED-SAVE `b3e89c9` (Jazz's, carried over) **+ `98d0473`: its high scores rerouted from the Sega CD's backup RAM to save piece 12 in bank 31** - without it the ED build saved nothing a player would notice. S: clone `HurricanCD32X_ED` (data via `HURRICAN_DATA=`), build line in its `buildcmd_edcd_save.txt`. **Not yet run.** |
| SuperTux REV | SuperTuxCD32X_REV | `CART_MB=ED EDCD=1 EDCD_SAVE=1` | `cd32x/subcpu/EDCD_SUPERTUX.32X` | `cd32x/subcpu/SuperTuxREV11-ED-T1.cue` | ED-SAVE `ab778e0` (CMD 69 - 62 is SET_VRAM there; `plat_save_read`; the probe's page-1 marker read from the bank; its `check_boot.py` md5s updated). S: clone `SuperTuxCD32X_ED` (data via `SUPERTUX=`), `buildcmd_edcd_save.txt`. **Not yet run.** |
| D32XR (Doom) | D32XR_CD32X | `CART=ED EDCD=1` | `cd32x/subcpu/EDCD_DOOM.32X` | `discs/D32XR_CD32X_R2_EDCD_DOOM/` | `cc0edfe` + ED-SAVE `3de1cc9` (always on with `CART=ED`: its 68000 program reads/writes bank 31 itself through CTRL1 - save piece 11, record piece 15; the launcher's Sub-CPU routes CMD 61; `EDCD_SAVE=1` exported for the RA header). Now **on S:** (`D32XR_CD32X`, the folder with the WADs): its 7 ED files copied, the two protected `Makefile`s edited in place, build line in `buildcmd_edcd_save.txt`. The S: copy was **ahead** of GitHub in four files (SIGIL in the ROM / `ROMMAPS`, the 4 MB board, the test switches): imported as `e4940b3` and `build.sh` merged in `05daa01`. Compiled here with stub headers, **never built with data nor run**. |
| Sonic CD 2011 | Sonic2011CD32X (private) | `CART_ED=1` | - | - | **Page plan and page switch done (26 Oct, R6, local commit); it cannot boot from an EverDrive yet.** `CART_ED=1`: the page switch is four word writes to `CTRL0-3`/`CTRL4-7` (command 27), page 7 is given up - the act-file slots 1 and 2 move to page 6 (banks 26, 27), the cart test stops at page 6, the menu's own pages are off (its tail was in page 7); the four act pages and all four slots are unchanged. Additions only: without the switch both files build byte for byte as before. **The blocker is mode 1**: this port is on Kobo's own listener, not the shared CD32X stack, so it has no `MODE1` switch - in cartridge mode the map is different (cart `0x000000`, Word RAM `0x600000`) and every cart write needs `W` turned on inside the copy, which `chilly_rv_memcpy`/`safe_cart_xfer` never do. That is a listener job (`hw_md.s`, the file that port's rule keeps shut) plus a `MODE1=1` Sub-CPU build fed to `edboot/build_edcd.sh`: the owner's call. Its `docs/R6_EVERDRIVE.md` has the detail. |

Build lines (WSL), all with `BOOTCAMP=/mnt/s/KoboPort/sega-toolchain-12.1/sega/CD32X-BootCamp`:
`CART=ED EDCD=1 EDCD_SAVE=1 BOOTCAMP=... bash .../OpenJazzCD32X-REV/build.sh`,
`HURRICAN_DATA=.../HurricanCD32X/HurricanData CART=ED EDCD=1 EDCD_SAVE=1 BOOTCAMP=... bash .../HurricanCD32X_ED/build.sh`,
`SUPERTUX=.../SuperTuxCD32X_REV/SuperTux-0.1.3 CART_MB=ED EDCD=1 EDCD_SAVE=1 BOOTCAMP=... bash .../SuperTuxCD32X_ED/build.sh`,
`CART=ED EDCD=1 BOOTCAMP=... bash .../D32XR_CD32X/build.sh` (ED-SAVE is always on with `CART=ED` there).
Each S: folder has the line ready in its `buildcmd_edcd_save.txt` (OpenLara: `buildcmd_edsave.txt`).

## ED-SAVE: done in every game (26 Oct, 09:35-11:10), proven only in OpenLara

All five now put page 1's save area in bank 31 with `EDCD_SAVE=1` (Doom: always with `CART=ED`).
The pattern (OpenJazz REV `243bd20` is the reference for the Jazz-shaped games): the launcher's
Sub-CPU program is compiled `-DCART_EDSAVE` with the listener's `M1SAVE=1`; its CMD 61 writes the
piece to cart `0x800000 + piece * 4 KB` (the listener's bank-31 route) when window 1 shows page 1
(`ed_page`, tracked in `cart_page_write`); a "piece in" command (62, SuperTux 69) brings a piece
back into the 4 KB buffer, CMD 57 hands out the words; the SH2 asks once (COMM2 = 0x8000, reply
bit 0x200) and reads the area through `plat_save_read()`; the disc is the same either way. Doom's
68000 program does the bank access itself (CTRL1 = bank 31, `0x480000 + piece * 4 KB`). Every
default Sub-CPU program and Doom's non-ED 68000 program were compared byte for byte.
**Only OpenLara's saves are console-proven**; Jazz/Hurrican/SuperTux/Doom ED discs have not run at
all yet. The WADs Doom keeps in pages 2-6 are PSRAM: copied at every boot, nothing can keep them.
Fast loading across relaunches cannot work (PSRAM wiped); within a session it does.

## Testing the save work in ares instead of on a disc (26 Oct, 11:32: "We don't need to burn discs
to test anymore")

The ares CD32X build with the EverDrive board makes the four untested ED-SAVE builds testable on the
PC. Two things it does NOT model decide what a green run there is worth:

1. **The PSRAM wipe is not modelled** (docs/11: "the wipe is not modelled - ares keeps the 16 MB").
   Banks 0-29 keep their contents across a return to the menu, so a save that survives a relaunch in
   ares proves **nothing** about ED-SAVE: the old page-1 path looks identical there. The discriminator
   is **where the bytes went**: with ED-SAVE working, the save is in bank 31, so the board's
   `bram.srm` holds it; if the game silently took the cart path instead, the game still shows the
   save in ares while `bram.srm` stays blank. Check the file, not the passport.
2. **The RV = 1 garbage is not modelled**: ares holds the SH2s while the 68000 has the bus, the
   console answers an SH2 cart access with garbage (`edsave3`, rule 8). A freeze of that class cannot
   appear in ares at all - it has to be found by reading the code, which is why the wait path was
   reviewed by hand (below).

### What each game actually keeps, and whether ED-SAVE carries it (checked 26 Oct, Opus)

Finding the save *code* correct is not the same as finding the game's save *covered* - they are
different questions, and for Hurrican the answers differed.

| Game | What it keeps | Through what | Carried to bank 31? |
|---|---|---|---|
| OpenLara REV | settings, game, 16 slots, records | cart pieces 0, 14 + its slot area | yes - console-proven |
| OpenJazz REV | `OPENJAZZ.CFG`, `SAVE.0-3` (Holiday Hare: its own 5-9), port settings, cache ledger | cart pieces 0-10, CMD 58/61 + reads through `plat_save_read` | yes (`243bd20`) |
| Hurrican | **the high-score table** - its only persistent state | **CMD 56 / 59: the CD BIOS's backup RAM** (`_BURAM`, `jsr 0x5F16`), file `OPENJAZZ_JJ` - *not* the cart save area | **no.** `b3e89c9` moved the cart save area, which Hurrican barely uses (piece 10, port settings). Its scores depended on the console's own internal backup RAM - which an EverDrive launcher does not wipe, but which OpenJazz and SuperTux both abandoned as not working on the console (BootCamp `docs/07`), and which is 8 KB shared by every game. `98d0473`: under `CART_EDSAVE` the Sub-CPU program routes CMD 56 / 59 to save piece 12 in bank 31 instead - engine unchanged, disc unchanged. `a3cbaf0` also gave the disc build's file its own name (`HURRICAN_HB`, was OpenJazz's) and 2 blocks instead of 64 |
| SuperTux REV | the save (piece 13), the probe's marker (15) | cart pieces, CMD 58/61 + `plat_save_read` | yes (`ab778e0`) |
| D32XR (Doom) | the save (piece 11), the preload record (15) | its own 68000 program (`crt0.s`) | yes (`3de1cc9`) |
| Sonic CD 2011 | everything (512 bytes) | **backup RAM only** (`SONICCD32X_`) | **no** - it keeps everything in the console's internal backup RAM (`SONICCD32X_`, 8 blocks). That survives an EverDrive launcher, but shares the console's 8 KB with every other game; a plan to get it to 2 + 4 blocks, with the reroute to bank 31 as the fallback, is in that repo's `docs/R5_SAVE_MINIMUM.md` (`ce69207`) |

The general rule, now in the BootCamp's `docs/02`: **the Sega CD's internal backup RAM has nothing to
do with the EverDrive's save bank.** Whatever ED-SAVE does to the cart save area, a title that keeps
its state in backup RAM saves nothing on a launcher build until CMD 56 / 59 are redirected to a save
piece. That redirect lives entirely in the Sub-CPU program, so it costs the engine nothing.

Settled since (MicroNut99 asked for it, 26 Oct 13:08): Hurrican's disc build no longer writes
OpenJazz's file. `a3cbaf0` names it `HURRICAN_HB` and declares 2 blocks rather than 64 - it had been
reserving 4 KB of the console's 8 KB for a 128-byte table. Both are deliberate breaks: scores saved
under the old name are not read back.

### The hand review of Fable's save work (26 Oct, 11:30-11:35, Opus) - no defects found

- **The word exchange**: CMD 57's index is masked to `BRAM_BYTES / 2 - 1` = 2047 against a 4 KB
  buffer, so `ed_fetch`'s 2048-word loop is in range (the handler's "128-byte buffer" comment is
  stale from Kobo's K36 - the code is right).
- **Both SH2s in SDRAM for every RV = 1 copy**: OpenJazz, Hurrican and SuperTux park the slave
  (`JOB->park`, wait for `JOB->parked`) *before* writing COMM0 and then wait in `sdram_wait`
  (`.data.sdram`, `noinline`, SR masked) - the order is right in all three. The new piece fetch
  (CMD 62 / SuperTux 69) goes through `send_cmd_rv`; the capability query and the 2048 word reads
  use plain `send_cmd`, which is correct - the Sub-CPU touches no cart for those.
- **Doom**: its SH2 CMD-IRQ wait is in `.sdata` (SDRAM) in `d32xr/crt0.s` - the loop spins on COMM0
  at `0x20004020` and only calls the C handler for a non-exit command, so nothing fetches the cart
  while the 68000 holds RV = 1. ED-SAVE changes only the address the 68000 touches (bank 31 through
  CTRL1 instead of page 1 through CTRL4-7), not the timing, and the page-7 form of that same path is
  console-proven - so the riskiest class of change was not made.
- **Arithmetic**: Doom's piece 11 -> `0x48B000` and the loader's record piece 15 (page-1 offset
  `0x1FF000`) -> `0x48F000` both land where the listener's bank-31 route puts them; `plat_save_read`
  offsets stay inside the 4 KB buffer everywhere (`off + n <= 4096`; SuperTux's `SAVE_BYTES` is 368);
  the only direct `0x223F0000` reads left are the non-ED fallbacks inside `plat_save_read`.
- Noted, not fixed: on a **4 MB** cart SuperTux's board probe would now call itself 8 MB, because
  page 1's marker goes to the bank while pages 2 and 6 go to the cart. The ED build only ever runs on
  the EverDrive's 16 MB, so it cannot be reached; leave it unless the ED build is ever cut to 4 MB.

## Tools

- `listener/ed_apply_jazzlike.py <game>` (also in the BootCamp `tools/`): applies the whole set
  of switches to a Jazz-shaped game (used for Hurrican); SuperTux and D32XR were done by hand.
- The ares CD32X build (Windows release `cd32x-v1.2-everdrive`, Settings → Options → EverDrive
  launcher) boots a launcher + disc on the PC; it holds the SH2s during RV = 1 (the console does
  not), so it proves logic, not the freeze. `ares-headless --ramcart-type everdrive --ramcart-boot
  EDCD_x.32X`. In this container only the Sub-CPU/listener/launcher parts of Jazz, Hurrican,
  SuperTux and Doom build (no sh-elf libstdc++, no game data); the engines build on S:.
- Probes (no disc): `probes/edtest`, `edw0`, `edsave`, `edsave2`, `edsave3` (docs/05).

## Gotchas met so far (so you don't meet them again)

- A build.sh hook anchored on the wrong line applied OpenLara's engine patch *after* the engine
  compiled → "same as the tech demo". Check `build_*_log.txt` order.
- `prep_openlara_cd.py` in the games' `cd32x/ref/openlara/tools/` or `tools/` must be OpenLara
  REV's (`--mode1`), or the ROM lacks its RAM jump table and the build prints usage and stops.
- SuperTux's `build.sh` takes the version name with `sed` from `game/stx.h`: no comment on the
  `#define STX_VERSION_*` line.
- `device_commit_files` may serve a stale staged file by path: stage under a new file name.
- The user's S: folders may be plain copies, not git checkouts, and may differ from GitHub **in
  either direction**: Doom's S: folder held four files no commit had (the SIGIL / ROM-map work).
  **Never overwrite a whole S: folder.** md5 every source file on S: against the repo's history
  first (`git log --format=%h -- <file>` and compare each blob); a file that matches no commit is
  work only S: has - import it, then merge (`git merge-file`), then copy back.
  The device shell cannot reach GitHub (proxy 403): copy changed files to S: with the device tools
  and compare md5s. The `Makefile`s on S: are protected: edit them in place with device python
  (idempotent - check for the switch's name first).
- SuperTux's `check_boot.py` strips the `#if STX_CART_PAGED` block up to the `#endif` before
  `cmd == 53` (ED-SAVE has `#ifdef CART_EDSAVE` inside); its md5 is in the script.

## Also in flight

- `Rayman_32X_RAM` (its own repo + S: folder): a Rayverse (CC0) port plan, hand-off in its `HANDOFF.md`.
- Open: Sonic CD 2011's mode-1 listener (the owner's go-ahead; its page plan and page switch are done); console
  results of the four ED discs; Doom built with data on S:.
