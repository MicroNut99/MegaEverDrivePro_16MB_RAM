# 00 – Everything it takes to build, and the commands

## What you need

| Thing | Where | Why |
|---|---|---|
| **The Sega toolchain** (Chilly Willy's): `sh-elf-gcc`, `m68k-elf-gcc`, `ldscripts/`, `bootblocks/` (`US_BOOT.BIN` etc.) | `ROOTDIR=/opt/toolchains/sega` by default, or `SEGA_TOOLCHAIN=<folder>` (then `<folder>/usr/bin` goes on PATH too). The projects here were built from `S:\KoboPort\sega-toolchain-12.1\sega` under WSL (`/mnt/s/...`) | compiles the SH2 side, the 68000 side, links with the right scripts, and the disc's boot block |
| `genisoimage`, `python3` with Pillow (`python3-pil`), `make`, `bash` | `sudo apt install genisoimage python3-pil` | the ISO, the images, the build scripts |
| **CD32X BootCamp** | `github.com/MicroNut99/CD32X-BootCamp` | the boot stack, the launcher builder (`edboot/build_edcd.sh`, `make_edcd.py`), the probes |
| **The game's project** (OpenLara CD32X REV for OpenLara) | `github.com/MicroNut99/OpenLaraCD32X-REV` (or `OpenLaraCD32X-REV_EDCD`, the same tree) | the disc, and the Sub-CPU program that goes into its `EDCD.32X` |
| **The game's data** | yours | levels, music, movies - never in a repository |
| **A Sega CD BIOS** | your own dump | only for ares (`bios/bios_CD_U.bin`) - the console has its own |

Folder layout the commands below assume (WSL):

```
/mnt/s/KoboPort/sega-toolchain-12.1/sega/
    CD32X-BootCamp/          the kit (BOOTCAMP= points here)
    OpenLaraCD32X-REV/       the game, flat layout: build.sh, subcpu/, sh2/, OpenLara-src/, the patches
```

In the OpenLara **repository** the same files sit under `OpenLaraCD32X/` with the patches in
`engine/`; its `build.sh` finds them either way.

## The commands

### The CD32X BootCamp disc from the EverDrive (the kit's own game)

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/CD32X-BootCamp
EDCD=1 bash build.sh                       # disc/BootCamp.cue + edboot/EDCD.32X
EDCD=1 EDCD_SAVE=1 bash build.sh           # + the save bank reachable (hw_md.s MODE1-7) and the RA header
DIAG=1 EDCD=1 bash build.sh                # the diagnostic disc + its launcher (green text)
```

`build.sh` first checks that the boot stack is the one that ran on the console
(`tools/check_boot.py`, `stack/BOOT_FILES.md5`): a changed stack stops the build.

### OpenLara CD32X from the EverDrive, saves on the SD card (the one to use)

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/OpenLaraCD32X-REV
CART=ED EDCD=1 EDCD_SAVE=1 BOOTCAMP=/mnt/s/KoboPort/sega-toolchain-12.1/sega/CD32X-BootCamp bash /mnt/s/KoboPort/sega-toolchain-12.1/sega/OpenLaraCD32X-REV/build.sh 2>&1 | tee /mnt/s/KoboPort/sega-toolchain-12.1/sega/OpenLaraCD32X-REV/build_edsave_log.txt
```

It makes:

| File | What to do with it |
|---|---|
| `subcpu/CDROMPlayer_cartED_music.cue` + `.bin` | burn it (data + CD audio) |
| `subcpu/EDCD.32X` | copy it to the EverDrive's SD card; start it from the EverDrive menu with the disc in the Sega CD (not "Run Mega-CD") |

The launcher belongs to the disc it was built with (it embeds that disc's Sub-CPU program).
A change to the engine means a new disc; a change to the Sub-CPU side means a new launcher.

Variants of the same line:

| Add | Makes |
|---|---|
| (nothing but `CART=ED EDCD=1`) | the game as before the SD-card saves: saves in the cart, lost at the next launch. `build.sh` takes the save-bank engine patch out of `OpenLara-src` for this build and puts it back with `EDCD_SAVE=1` |
| `EDCD_DIAG=1` | the same launcher with the Sub-CPU's green text on: boot steps, the heartbeat, the save commands traced on row 21 (`T62 P102 S3 57:... 58:... 61:... 62:...`), all eight COMM registers on rows 19-20 in the game. The disc is the same |
| `EDCD_NOVERIFY=1` | (diagnostic) CMD 61 skips its read-back of the save bank |
| `EDCD_X=1` | CTRL0 with the EverDrive's X bit (`0xC000`); changed nothing on the console |
| `EDCD_W0=<bank>` | the game in that EverDrive bank instead of 0 |
| `M1WORDS=1` | the listener copies into the cart in words where it can |

What `EDCD_SAVE=1` does, in one line each: the MODE1 listener gets `M1SAVE=1` (cart
`0x800000`-`0x83FFFF` = bank 31 through window 1), the Sub-CPU gets `EDSAVE=1` (CMD 61/62 use the
bank), the engine gets `openlara-edsave.patch` (reads through the Sub-CPU, slot cache, SH2 parking),
and `make_edcd.py` gives the launcher the `RA` header entry so the EverDrive OS keeps `bram.srm`.

### The probes (no disc, no game data)

```bash
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/CD32X-BootCamp
bash edtest/build.sh      # edtest/EDTEST_W.32X, EDTEST_X.32X   the 16 MB as RAM, all banks, both SH2s
bash edw0/build.sh        # edw0/EDW0E_W.32X                   window 0 as the SH2 reads it while the 68000 writes
bash edsave/build.sh      # edsave/EDSAVE1.32X                 what survives the EverDrive menu (run twice)
bash edsave2/build.sh     # edsave2/EDSAVE2.32X                the save bank worked as the listener does, SH2 busy
```

Copy the `.32X` to the SD card, start it from the EverDrive menu with the 32X attached. Built
copies are in `probes/` here.

## Testing without the console: ares CD32X for Windows

The ares CD32X build (ares v148 with the RAM carts and the EverDrive model) is the test bench:
every launcher and disc above was run in it before it went to the console, and after the day's
findings it fails the launchers the console fails. Windows x64 builds are published as releases
of the ares CD32X repository (`cd32x-latest`, and the tagged `cd32x-v1.x-everdrive` releases);
`ares/README-CD32X.md` here is its manual.

The cartridge boot in the window, with the pad in hand:

1. `bios\bios_CD_U.bin` next to `ares.exe` (your dump).
2. **Settings → Options → Mega Drive Settings**: cart type **Mega EverDrive PRO**, a cart number,
   and **EverDrive launcher → Browse** to your `EDCD.32X`.
3. **Load → Sega → Mega CD 32X**, the disc's `.cue`.

The disc boots from the launcher exactly as on the console. The EverDrive's 16 MB - including
bank 31, where the saves are - is the cart file `Systems\EverDrive PRO 16 MB-<n>.ram`, so a save
survives a reload the way `bram.srm` does on the card. Leave the launcher field empty for the
older "disc boot with the cart as a launcher left it" model.

The same thing scripted, for regression runs (the Linux `ares-headless` of the same build):

```bash
ares-headless --game disc.cue --machine cd32x --bios-us bios_CD_U.bin --home ./home \
    --ramcart cart.bin --ramcart-type everdrive --ramcart-boot EDCD.32X \
    --frames 4400 --press 3700:A --press 3730: --screenshot 4300:title.ppm
```

`--press frame:buttons` holds buttons from that frame on (`UDLRABCXYZMS`, empty = release),
`--screenshot frame:file.ppm` grabs a frame, and the trace prints the COMM registers once a
second (`[12]=` is the Sub-CPU's status word: `4B10` = the game runs).

What ares does **not** reproduce: the EverDrive menu wiping the PSRAM (ares keeps the 16 MB in the
cart file), the exact timing of the W = 1 disturbance (modelled as a 1 µs window), and the slave
SH2 lock-up of a 68000 copy during a PWM DMA read (the console's rule 5 - ares ran the unparked
saves fine). The console remains the judge; ares is where the boot and the data paths are proven
first.
