# Mega EverDrive PRO as a 16 MB RAM cart for the Sega CD 32X

Everything learned and built to make the **Mega EverDrive PRO** serve as the 16 MB RAM cart of a
CD32X disc (Genesis + Sega CD + 32X): the facts measured on the console, the launcher that boots a
disc from the EverDrive, the 68000 listener that works the cart the way the console demands, the
saves on the EverDrive's SD card, the probes that found each rule, and the ares model that
reproduces the hardware. Proven on a console with the CD32X BootCamp disc and OpenLara CD32X,
25 October 2026.

Nothing in here is game data, a BIOS or a disc image. The sources are copies of the folders that
live in the projects they came from (CD32X BootCamp, OpenLara CD32X REV, the ares CD32X build);
this repository is the one place that holds all of it with the explanation.

## Read in this order

| Document | What it tells |
|---|---|
| [docs/00_BUILD.md](docs/00_BUILD.md) | **Everything it takes to build and the exact commands**: the toolchain, the two project trees, every switch; the probes; and ares CD32X for Windows as the test bench, with the cartridge boot in its window. |
| [docs/01_THE_EVERDRIVE_AS_RAM.md](docs/01_THE_EVERDRIVE_AS_RAM.md) | The EverDrive's mapper, registers, banks, which memory is PSRAM and which goes to the SD card. The console's three rules. |
| [docs/02_CARTRIDGE_BOOT.md](docs/02_CARTRIDGE_BOOT.md) | Why a CD boot cannot work, and how `EDCD.32X` boots the disc from the cartridge: the 32X hook, the Sega CD brought up by the launcher, the hand-over to the disc's own boot stack. |
| [docs/03_THE_LISTENER.md](docs/03_THE_LISTENER.md) | The 68000 listener for the cartridge boot (`MODE1`): the address map, the window-1 route, the write-enable bit, the save bank, every switch. |
| [docs/04_SAVES_ON_THE_SD_CARD.md](docs/04_SAVES_ON_THE_SD_CARD.md) | Why saves in the cart vanish, bank 31 and `bram.srm`, how OpenLara reads and writes its saves through the Sub-CPU, and why every save command parks the slave SH2. |
| [docs/05_PROBES_AND_FINDINGS.md](docs/05_PROBES_AND_FINDINGS.md) | `edtest`, `edw0`, `edsave`, `edsave2`: what each ROM does and what the console answered. The chronology of the day. |
| [docs/06_ARES_MODEL.md](docs/06_ARES_MODEL.md) | The EverDrive board in ares: what is modelled, what is a choice, how to run the cartridge boot in the window and headless. |
| [docs/07_YOUR_OWN_GAME.md](docs/07_YOUR_OWN_GAME.md) | **`EDCD.32X` is a hook for *your* CD game**: what it carries, why there is one per disc, and how to make one for a game on the stack, step by step - and what to do about its saves. |

The originals these were written from are kept unchanged: `bootcamp-docs/` (the CD32X BootCamp's
docs 01, 04, 06, 09, 10, 11, 12), `openlara/README-EDCD.md`, `openlara/README-EVERDRIVE.md`,
`ares/README-CD32X.md`.

## The folders

| Folder | Contents | From |
|---|---|---|
| `launcher/` | `EDCD.32X` sources: `edcd_md.s` (the 68000 program), `edcd_sp.s` (the Sub-CPU start-up), `make_edcd.py` (the ROM, header, 32X hook), `build_edcd.sh`; the earlier `EDBOOT`/`EDPROBE` experiments; `EDCD_BOOTCAMP.32X` built for the BootCamp disc | CD32X BootCamp `edboot/` |
| `listener/` | `hw_md.s`, the 68000 listener of the boot stack with the `MODE1` cartridge-boot build and its switches; the Sub-CPU `Makefile`; the BootCamp `build.sh` (its `EDCD=1` block); `prep_rom.py --mode1` | CD32X BootCamp `stack/subcpu/`, `tools/` |
| `probes/` | `edtest` (the 16 MB as RAM), `edw0` (the write-enable rule), `edsave` (what survives the menu), `edsave2` (the save bank as the listener works it, SH2 busy) - sources and the built ROMs | CD32X BootCamp |
| `openlara/` | OpenLara's Sub-CPU program with the save commands (`subcpu/main.c`: CMD 61, 62, 57, 58), its listener and Makefile, the engine patch `openlara-edsave.patch` (saves through the Sub-CPU, slot cache, SH2 parking), `build.sh` with the `EDCD` / `EDCD_SAVE` / `EDCD_DIAG` blocks, the readmes | OpenLara CD32X REV |
| `ares/` | `everdrive-pro-core-v148.patch` (the whole EverDrive core against upstream ares v148, as of 25 Oct 2026), the three files it changes most for reading, `README-CD32X.md` (the manual), the release notes | the ares CD32X build |

## The `.32X` is a hook for your CD game

`EDCD.32X` is not a generic launcher: it carries the disc's own Sub-CPU program (built for the
cartridge boot) and its SH2 loader, and its 32X header is copied from that loader's CD boot table -
so the 32X's boot ROMs load the disc's loader from the cartridge exactly as the Sega CD BIOS would
from the disc. One launcher per disc; rebuild it when the Sub-CPU side changes. Any game on the
CD32X BootCamp stack gets one from its own `build.sh` with `EDCD=1`, or by hand in four steps
([docs/07](docs/07_YOUR_OWN_GAME.md)). A game that is not on the stack has to be moved onto it first.

## Testing on a PC: ares CD32X for Windows

The ares CD32X build (ares v148 with the RAM carts and this EverDrive model) runs the whole thing
in a window: cart type *Mega EverDrive PRO*, **Settings → Options → EverDrive launcher** pointed at
`EDCD.32X`, load the disc - the cartridge boot as the console does it, pad in hand, the saves in
the EverDrive cart file. Every launcher and disc here went through it before the console, and
after the day's findings it fails the launchers the console fails. The launcher field is in the Windows build from v1.2 on (`cd32x-v1.2-everdrive`); before it, the
Windows window could only boot the disc the normal way, with the EverDrive as a plain RAM cart -
the disc's own Sub-CPU program, saves in the cart, no different from a battery-backed cart. Windows x64
builds are the `cd32x-latest` and `cd32x-v1.x-everdrive` releases of the ares CD32X repository; `ares/README-CD32X.md`
is its manual and `docs/06_ARES_MODEL.md` says what it models and what it does not.

## Build

Building a launcher needs the game's project (its Sub-CPU program is embedded in `EDCD.32X`) and
the Sega toolchain the CD32X projects use. From the CD32X BootCamp:

```bash
EDCD=1 bash build.sh                 # the disc + edboot/EDCD.32X
EDCD=1 EDCD_SAVE=1 bash build.sh     # + the save bank (bank 31, the SD card) reachable, RA header
```

From OpenLara CD32X REV (the BootCamp next to it or `BOOTCAMP=<folder>`):

```bash
CART=ED EDCD=1 EDCD_SAVE=1 bash build.sh          # the disc + subcpu/EDCD.32X, saves on the SD card
CART=ED EDCD=1 EDCD_SAVE=1 EDCD_DIAG=1 bash build.sh   # the same launcher with the green trace on
```

The probes build on their own: `bash probes/edtest/build.sh`, `bash probes/edw0/build.sh`,
`bash probes/edsave/build.sh`, `bash probes/edsave2/build.sh` (no disc, run from the EverDrive menu
with the 32X attached).

## The facts in one place (all measured on a console, 24-25 Oct 2026)

1. **The EverDrive stays the cartridge.** Its `C` bit does not release CART_IN; a CD boot with it in
   the slot is impossible. The disc is booted *from* the cartridge: `EDCD.32X` starts the Sega CD
   itself and hands over to the disc's unchanged boot stack (the Sega CD's "mode 1").
2. **Extended SSF mapper**: `0xA130F0` CTRL0 = `P X W L C . . . . . . R R R R R` (taken only with
   P), `0xA130F2`-`FE` = the bank in windows 1-7 of cart `0x080000`-`0x3FFFFF`. Register writes
   need RV = 1 with the 32X on. All 27 usable banks hold what the 68000 writes; both SH2s read them.
3. **The write-enable bit W must be off** whenever the 68000 writes anything but the cart: with it on,
   68000 writes to work RAM, the VDP or the gate array land in the cart and change it, and the SH2's
   reads of the cart come back as zeros meanwhile. The listener turns W on only inside a copy into
   the cart, with the SH2s held off by RV = 1.
4. **Window 0 (`0x000000`-`0x07FFFF`) cannot be written by the 68000 directly** with the 32X on; the
   listener writes it through window 1 with window 0's bank mapped there.
5. **Word RAM and the cart never meet with the same RV**: Word RAM is read with RV = 0, the cart
   written with RV = 1, a 1 KB work-RAM buffer in between.
6. **Banks 0-29 are PSRAM and the EverDrive OS wipes them every time its menu runs.** Nothing a game
   leaves there is seen again after a relaunch: no saves, no "level already in the cart".
7. **Bank 31 is the EverDrive's battery RAM**, kept on the SD card as
   `MEGA/gamedata/<launcher>/bram.srm` (256 KB) when the launcher's header declares save RAM. The
   saves go there, through the Sub-CPU (the SH2s cannot see that bank).
8. **Before any 68000 copy with RV = 1 in a running game, park the slave SH2 and stop its PWM
   mixer** - exactly what the level loads always did. A copy while the slave draws and its sound
   DMA reads the cart locks both SH2s.

krikzz's own mapper source (github.com/krikzz/MEGA-PRO, `fpga/mapper/map_ssf/map_ssf.sv`) agrees
with 2 and 7: banks 0-15 and 16-30 are the two 8 MB PSRAM chips, bank 31 is the backup memory.

## Credits

The CD32X BootCamp, OpenLara CD32X REV and the ares CD32X build by MicroNut99, with Claude.
Chilly Willy's 32X start-up code and the D32XR loader are the base of every 32X side here.
krikzz for the EverDrive PRO and for publishing its mapper source.
