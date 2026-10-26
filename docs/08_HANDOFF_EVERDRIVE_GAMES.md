# 08 – Hand-off: building CD32X games for the Mega EverDrive PRO

Written 26 Oct 2026, 09:30, for the next chat. The person you work with (MicroNut99) owns the
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
| OpenJazz REV | OpenJazzCD32X-REV | `CART=ED EDCD=1` | `cd32x/subcpu/EDCD_JAZZ.32X` | `cd32x/subcpu/OpenJazzCD32X_cartED.cue` | Built on S: this morning (after a missing `--mode1` in its `prep_openlara_cd.py`, fixed). **Not yet run on the console.** |
| Hurrican | HurricanCD32X | `CART=ED EDCD=1` | `cd32x/subcpu/EDCD_HURRICAN.32X` | `cd32x/subcpu/HurricanP1q_cartED.cue` | In GitHub; the S: folder is a fresh clone `HurricanCD32X_ED` (data via `HURRICAN_DATA=`). **Not yet run.** |
| SuperTux REV | SuperTuxCD32X_REV | `CART_MB=ED EDCD=1` | `cd32x/subcpu/EDCD_SUPERTUX.32X` | `cd32x/subcpu/SuperTuxREV11-ED-T1.cue` | Clone `SuperTuxCD32X_ED` on S: (data via `SUPERTUX=`). Levels kept in pages 2-6 like the 8 MB build (no first-start copy). **Not yet run.** |
| D32XR (Doom) | D32XR_CD32X | `CART=ED EDCD=1` | `cd32x/subcpu/EDCD_DOOM.32X` | `discs/D32XR_CD32X_R2_cartED/` | Committed `cc0edfe`, compiled here, **never built with data nor run**. Its game's second 2 MB go to page 1 (`PAGE=1`); its own 68000 program (`d32xr/src-md/crt0.s`, `CD32X_ED`) writes the window registers and the write bit. Not on S: yet. |
| Sonic CD 2011 | Sonic2011CD32X (private) | - | - | - | **Not started.** Its 16 MB layout puts act-file slots and the menu's tail in page 7 (the fourth slot is exactly bank 31): needs a new page plan first, and its own rule - every disc proven in its sh2emu model (which models the PCB mapper, not the EverDrive's windows). |

Build lines (WSL), all with `BOOTCAMP=/mnt/s/KoboPort/sega-toolchain-12.1/sega/CD32X-BootCamp`:
`CART=ED EDCD=1 BOOTCAMP=... bash .../OpenJazzCD32X-REV/build.sh`,
`HURRICAN_DATA=.../HurricanCD32X/HurricanData CART=ED EDCD=1 BOOTCAMP=... bash .../HurricanCD32X_ED/build.sh`,
`SUPERTUX=.../SuperTuxCD32X_REV/SuperTux-0.1.3 CART_MB=ED EDCD=1 BOOTCAMP=... bash .../SuperTuxCD32X_ED/build.sh`,
`CART=ED EDCD=1 BOOTCAMP=... bash .../D32XR_CD32X/build.sh` (OpenLara: `buildcmd_edsave.txt` in its folder).

## What is NOT done for any game but OpenLara

- **Saves do not persist**: Jazz, Hurrican, SuperTux and Doom still write their saves (and
  Doom its preload record - so Doom copies 10 MB of WADs from the CD at every boot) into the
  cart's page 1, which the EverDrive wipes at its menu. The fix is OpenLara's EDSAVE-1 route
  (docs/04): the save area through the Sub-CPU to bank 31 (CMD 61/62 + the listener's
  `M1SAVE=1`, `EDCD_SAVE=1` adds the `RA` header), game by game.
- Fast loading across relaunches cannot work (PSRAM wiped); within a session it does.

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
- The user's S: folders may be plain copies, not git checkouts, and may be older than GitHub.
