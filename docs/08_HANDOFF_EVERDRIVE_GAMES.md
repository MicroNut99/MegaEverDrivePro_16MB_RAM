# 08 – Hand-off: building CD32X games for the Mega EverDrive PRO

Written 26 Oct 2026, 09:30, updated 11:25 (ED-SAVE in every game; Doom on S:), for the next chat. The person you work with (MicroNut99) owns the
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
  name tag." So a new EverDrive build names both its launcher and its disc with an `EDCD` tag and
  the variant, e.g. Doom: `EDCD_DOOM.32X` + `D32XR_CD32X_R2_EDCD_DOOM`, `EDCD_DOOM_SIGIL.32X` +
  `..._EDCD_DOOM_SIGIL` (`MUSIC=sigil`), `EDCD_DOOM_ROMMAPS.32X` (`ROMMAPS=`), and a launcher that
  is *not* the EverDrive's gets a name of its own (`EDCD_DOOM_PAGE7.32X`) - the EverDrive keeps each
  launcher's saves under its own name, so two variants must never share one. The four earlier games'
  discs carry `_cartED` / `-ED-T1` instead (already unique, already built): rename only if asked.
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
| OpenJazz REV | OpenJazzCD32X-REV | `CART=ED EDCD=1 EDCD_SAVE=1` | `cd32x/subcpu/EDCD_JAZZ.32X` | `cd32x/subcpu/OpenJazzCD32X_cartED.cue` | ED-SAVE `243bd20` (CMD 61 to bank 31, CMD 62 piece in). On S:. **Not yet run on the console.** |
| Hurrican | HurricanCD32X | `CART=ED EDCD=1 EDCD_SAVE=1` | `cd32x/subcpu/EDCD_HURRICAN.32X` | `cd32x/subcpu/HurricanP1q_cartED.cue` | ED-SAVE `b3e89c9` (Jazz's, carried over). S: clone `HurricanCD32X_ED` (data via `HURRICAN_DATA=`), build line in its `buildcmd_edcd_save.txt`. **Not yet run.** |
| SuperTux REV | SuperTuxCD32X_REV | `CART_MB=ED EDCD=1 EDCD_SAVE=1` | `cd32x/subcpu/EDCD_SUPERTUX.32X` | `cd32x/subcpu/SuperTuxREV11-ED-T1.cue` | ED-SAVE `ab778e0` (CMD 69 - 62 is SET_VRAM there; `plat_save_read`; the probe's page-1 marker read from the bank; its `check_boot.py` md5s updated). S: clone `SuperTuxCD32X_ED` (data via `SUPERTUX=`), `buildcmd_edcd_save.txt`. **Not yet run.** |
| D32XR (Doom) | D32XR_CD32X | `CART=ED EDCD=1` | `cd32x/subcpu/EDCD_DOOM.32X` | `discs/D32XR_CD32X_R2_EDCD_DOOM/` | `cc0edfe` + ED-SAVE `3de1cc9` (always on with `CART=ED`: its 68000 program reads/writes bank 31 itself through CTRL1 - save piece 11, record piece 15; the launcher's Sub-CPU routes CMD 61; `EDCD_SAVE=1` exported for the RA header). Now **on S:** (`D32XR_CD32X`, the folder with the WADs): its 7 ED files copied, the two protected `Makefile`s edited in place, build line in `buildcmd_edcd_save.txt`. The S: copy was **ahead** of GitHub in four files (SIGIL in the ROM / `ROMMAPS`, the 4 MB board, the test switches): imported as `e4940b3` and `build.sh` merged in `05daa01`. Compiled here with stub headers, **never built with data nor run**. |
| Sonic CD 2011 | Sonic2011CD32X (private) | - | - | - | **Not started.** Its 16 MB layout puts act-file slots and the menu's tail in page 7 (the fourth slot is exactly bank 31): needs a new page plan first, and its own rule - every disc proven in its sh2emu model (which models the PCB mapper, not the EverDrive's windows). |

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
- Open: Sonic CD 2011 page plan; console results of the four ED discs; Doom built with data on S:.
