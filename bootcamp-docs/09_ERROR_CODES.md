# 09 – Error codes: look them up here

Every number the BootCamp disc can show, what it means, and where it comes from. Each entry points back
to a tag or a line of text you can grep:

- `BC-Ex` in `hello/src/cdapi.c`;
- the printed text in `stack/subcpu/main.c`;
- the code value in `stack/sh2/cdloader_sh2.c`.

## 1. Where the numbers show

| Disc | What you see | Built with |
|---|---|---|
| **Release** | the boot picture, then hello. Any failed CD service opens hello's **red error box**. | `bash build.sh` |
| **Diagnostic** | the white 32X start-up lines and the Sub-CPU's **green text from the SEGA logo on**, in front of the boot picture: every boot step, `STATE:` (COMM12), every file and cart chunk. A boot that stops leaves its last lines on screen. Once hello runs it stays on that text: a one-line **PAD** readout (the directions and buttons held, in words) and **B+C** to switch to hello's screen and back. hello's buttons act when released, as a chord, so B+C never also stops the music or loads a file. An error switches to hello's screen, so no green row covers the box (except after `4EE4`: the Sub-CPU is not answering, so the display cannot be switched). | `DIAG=1 DISC_NAME=BootCampDiag bash build.sh` |
| **ares** | COMM12 at any moment: Android's *Show fps and COMM12 STATE*, or the once-a-second line of `ares-headless` | either disc |

**How the diagnostic disc shows text before the game.** On the release disc the Genesis display is off
during the boot:

- `hw_md.s` switches it off at the end of the 68000's set-up (`OL-13`);
- CDLOADER sends CMD 52 with COMM2 = 0 at `4D01` (`OL-3`).

`DIAG=1` builds the disc with four diagnostic lines in place of their release lines, for that build only.
`tools/diag_switch.py` lists them (`check` shows their state):

| Switch | Release line | Diagnostic line | Effect |
|---|---|---|---|
| `main.c` | `KOBO_DIAG_TEXT 0` | `1` | the green text is written |
| `main.c` | `KOBO_BOOT_TEXT 0` | `1` | `main()` does not switch the display off |
| `hw_md.s` | `move.w #0x8134` | `#0x8174` | the 68000 leaves the display on |
| `cdloader_sh2.c` | CMD 52 `COMM2 = 0` | `COMM2 = 1` | CDLOADER keeps the text on at `4D01` |

The Genesis text then sits **in front of** the boot picture: the 32X's priority bit (bit 7 of `0x20004100`)
stays 0, which per the 32X manual gives the Mega Drive screen priority. The source in git always holds the
release lines, and `tools/check_boot.py` accepts either form.

## 2. "It stopped here": the screen tells you the stage

| What the screen shows | Stage | COMM12 (ares) | Look at |
|---|---|---|---|
| The BIOS screen, the disc is not started | the BIOS rejected the disc | — | the disc was burnt as ISO only (burn the `.cue`); the region of `US_BOOT.BIN` vs the console's; a bad burn |
| The SEGA logo, then black, forever | the Sub-CPU program, the 68000 listener, or the 32X start-up | `0000` | is the 32X on and seated? The boot stack unchanged (`check_boot.py`)? In ares: does COMM10 reach `0BBB`? |
| The boot picture, forever | CDLOADER stopped while loading the ROM | `4C00`+n or `4DEx` (`4D06` only for a while: an unanswered command is given up after about 20 s) | §3. The usual cause: no RAM cart, or a cart that does not keep what is written (`4DE2`) |
| The boot picture, then black, forever | the hand-over or your `crt0.s` / `main()` | `4D05` | your ROM's entries at `0x3E0`; a `.bss` that runs over the hand-over routine at `0x0603C000` ([04](04_MEMORY_MAPS.md)); a wait in your start-up that never ends |
| hello's screen, red box | a CD service failed | `4EEx` | §4 |
| hello's screen, frozen | a wait without a timeout, or a crash on the SH2 | the last code posted | every wait bounded ([06](06_HARDWARE_RULES.md) rule 7) |

The footer of hello's boot picture says this too: *Stuck on this picture? Look up:
docs/09_ERROR_CODES.md*.

## 3. CDLOADER (`stack/sh2/cdloader_sh2.c`, `show_status()` / `halt()`)

| COMM12 | Meaning | If it stops here |
|---|---|---|
| `4D01` | CDLOADER started. The Genesis display is off (CMD 52); `IMAGE.RAW` is shown (CMD 38 file 0, CMD 51). | — |
| `4D06` | Checking the cart: CMD 47 compares the first, middle and last ROM chunks. The answer is ignored (quick check off), so it always copies. | it does not stop here for good: CDLOADER gives up on an unanswered command after a long wait and goes on |
| `4C00`+n | Chunk n (32 KB) of `GAME.32X` copied and read back. `4C0A` = chunk 10. | the copy of chunk n+1 hung: the cart or the 68000 |
| `4D02` | All chunks loaded and verified | — |
| `4D03` | The header checks out: `SEGA` at `0x100`; at `0x3D4` the SDRAM image (size 1 … 192 KB, destination + size ≤ 224 KB); the entries' top byte `0x06` | — |
| `4D04` | The slave SH2 is parked in the hand-over routine at `0x2603C000` | — |
| `4D05` | Hand-over: both SH2s jump to your entries | your start-up (§2) |
| **`4DE1`** | `GAME.32X` is not on the disc (CMD 38 file 20 answered length 0) | the name in `game_files.h` (`GAME_ROM_NAME`); `build.sh` step 5 checks it |
| **`4DE2`** | A chunk did not read back the same: the cart did not keep it | no RAM cart in the 32X slot; a bad cart or battery; a scratched disc. On the diagnostic disc, the green row 17 says where: `MISMATCH Cn +offset DISC xx CART yy`. |
| **`4DE3`** | Not a 32X ROM, or a bad header | `tools/prep_rom.py` makes the same checks at build time: rebuild and read its output |
| **`4DE4`** | The slave did not park | never seen with this loader: report it with your disc |

## 4. hello and `cdapi.c`: the error box

```
ERROR 4EE3  FILE DID NOT REACH CART        <- the code, and what it means
BC-E3 CDAPI.C CD_LOAD                      <- the tag: grep "BC-E3" in hello/src
CMD 46 FILE 100 CHUNK 2 +010000            <- what was asked: command, file, chunk, offset in the file
C0 0000 C2 0003 C4 0000 C6 0000            <- the COMM registers when it failed
C8 0001 CA 2000 CC 0001 CE 3A1F
LOOK UP: DOCS 09 ERROR CODES
```

| Code | Tag | Meaning | Fields | Causes and fixes |
|---|---|---|---|---|
| `4E00` | | hello started | | — |
| `4E01` | | the boot counter was saved | | — |
| **`4EE1`** | `BC-E1` `cart_save` | CMD 61 did not answer 1: the 68000 wrote the save piece, read it back, and it differed | `PIECE` n, `CART` = `0x3F0000 + n × 0x1000` | no RAM cart; the cart is write-protected; a flat battery. On the diagnostic disc, row 25 says `SAFE CART: FOUND` or `NOT FOUND`. |
| **`4EE2`** | `BC-E2` `cd_open` | CMD 38 answered length 0: the file is not on the disc | `FILE` = the number asked for | the number is not in `game_files.h` (100 + n, 200 + n, 300 + n), or the file is not in `<game>/cd/`. Diagnostic row 12: `ERR: <NAME> NOT FOUND`. |
| **`4EE3`** | `BC-E3` `cd_load` | CMD 46 answered COMM12 ≠ 0: a chunk did not reach the cart | `FILE`, `CHUNK` c, `+` = `c × 0x8000` (the offset in the file) | as `4DE2`. Diagnostic row 13: `CART I<slot> C<chunk> @<68000 address> BAD`. Row 17: the first byte that differs. |
| **`4EE4`** | `BC-E4` `cd_cmd` | a command was not answered within 10 s (COMM0 never went back to 0) | `CMD`, `COMM2` = its argument | a command number this Sub-CPU does not have: it is never answered (`main.c` has no catch-all), so check `CMD` against [03](03_COMMANDS.md). Otherwise the Sub-CPU is busy or stopped: is `CE` (its heartbeat) changing? |
| `4B10` | | running, no error | | — |

**Which error the box shows.** The first one: later errors are usually its consequences. The exception is
`4EE4`: a Sub-CPU that stopped answering explains everything after it, so it replaces an earlier error.
`4EE4` is final. Every later wait gives up after 0.1 s, and nothing clears it: reboot. A later successful
load clears `4EE2` / `4EE3`, and a successful save clears `4EE1`.

**The register line** (the 32X COMM registers, `0x20004020` + n):

| | Register | What it tells you |
|---|---|---|
| `C0` | COMM0 | the command still pending: not 0 means the Sub-CPU had not finished it |
| `C2` | COMM2 | the argument of the last command |
| `C4`, `C6` | COMM4, COMM6 | the game's own (master ↔ slave, renderer) |
| `C8`, `CA` | COMM8, COMM10 | the last reply: a file length (CMD 38), or the pad and the 68000's tick count (CMD 48) |
| `CC` | COMM12 | taken just before the error code is posted. CMD 46: `0001` = the chunk was bad, `FFFF` = no reply. CMD 61: `0000` = failed or no reply (the error code tells which). Any other command: the status hello had posted. |
| `CE` | COMM14 | **the Sub-CPU's heartbeat** (main.c `HW8`). The top digit is the CD drive's state: `0` stopped, `1` playing, `2` seeking, `3` scanning, `4` paused, `5` tray open, `9` reading the TOC, `B` no disc or error. The other three digits are its pass count ÷ 64. Two error boxes with the same `CE` mean the Sub-CPU has stopped. |

## 5. The green text, row by row (diagnostic disc)
Printed by the Sub-CPU: `print_diag(…, row)` in `stack/subcpu/main.c`, and rows 2, 16, 17 and 19–25 from `scd.c`. Grep
the text to find the code.
Numbers are hex unless said otherwise.

| Row | Text | Meaning |
|---|---|---|
| 0 | `BUILD: D32XR CD M1C QUICK CHECK` | the Sub-CPU program started |
| 1 | `POST-INITCD COMM: nn` | the 68000's command byte after the CD drive started (`00` = idle, as it should be) |
| 2 | `LOADING 32XCD ENGINE...`, then `INSIDE-INITCD RESULT: n` (`scd.c`) | the CD drive's start-up: `0` = ok, negative = it failed (no disc, drive error) |
| 3 | `POST-MEMCPY2 COMM: nn` | as row 1, after CDLOADER was copied |
| 4 | `32X PAYLOAD: n BYTES SH2:…` | CDLOADER's size (decimal) as built into `APP.BIN` |
| 5–9 | `CD DRIVE READY`, `BOOTING SUB-CPU...`, `INITIALIZING SUB-CPU Sfx...`, `68K LISTENER ACTIVE`, `BOOT COMM PORT: nn` | the start-up steps, in order. The last one shown is where it stopped. |
| 10 | `32X:cc 32XReg:rrrr STATE:ssss CKPT:kkkk` | COMM0 (the command), COMM4, **COMM12 (the status codes of §3–4)**, COMM8's high word. Redrawn when the status changes, until `4B10`. |
| 11 | `OPENING <NAME>...` | CMD 38: the file name the number became |
| 12 | `<NAME> n BYTES OK` / `ERR: <NAME> NOT FOUND` | its length (decimal); `(TOO TALL)` = over 128000 bytes, a note for pictures only |
| 13 | `CART I<slot> C<chunk> @aaaaaa OK` | CMD 46: slot and chunk (decimal); `@` = the 68000 address written (`0x400000` + the cart offset). `BAD` = read back wrong, `EMPTY` = nothing read, `NOVFY` = not checked (bit 15). `CMP` instead of `CART` = CMD 47. |
| 14 | `CD TRACKS ff-ll TRACK2 xxxxxxxx` | the disc's first and last track, and track 2's position |
| 15 | `ITER1 COMM PORT: nn` | the 68000's command byte on the main loop's first pass |
| 16 | `32X WIN C<chunk> 900000: OK` / `… W<want> R<got>` | the chunk read back through the 68000's `0x900000` window; also `CD->FB CHUNK c n SECTORS` for CMD 50 / 51 (the boot picture) |
| 16 | `CKPT1: entered scd_open_file` (`scd.c`) | a file open started |
| 17 | `COMM:nn CMD:c TIMEOUT` (`scd.c`) | the 68000 did not take the Sub-CPU's command `c` in time (its command byte `nn` stayed set) |
| 17 | `MISMATCH C<n> +ooooo DISC xx CART yy` | the first byte of chunk n that differs. **On a first boot, `MISMATCH C0 +00000 … CART FF` is normal:** the quick check found an empty cart. |
| 18 | `POST-SENDMD COMM: nn`, `SH2 REACHED THE LDC LINE`, `SH2 NEVER EVEN REACHED THIS FILE` | start-up checkpoints (COMM4 = `0x0EEE` / not `0x1234`) |
| 19 | `INIT32X:r 1ST:xxxx` | the 32X start-up result: `0` ok; `1` the master SH2 never answered `M_OK`, `2` the slave never answered `S_OK`, `3` neither (the boot goes on); `-2` no 32X (no `MARS` at `0xA130EC`), `-3` the 32X's SDRAM failed its test (`SDER`), `-5` the adapter never became ready |
| 19 | `SH2 READY FOR ISOLATED TEST: 0AAA` | CDLOADER's handshake arrived (anything else: it did not) |
| 19 | `Sssss C0:cccc ST:ssss M:mmmm SL:llll` | the heartbeat while waiting for the game: pass count, COMM0, COMM12, COMM6, COMM10. It stops at `4B10`. |
| 20 | `END C0:… C4:…` / `SH2 NEVER SIGNALLED READY - STOPPED` | the boot ROMs' last words; or no `0x0AAA` within 3 s (180 ticks): the Sub-CPU stops |
| 21 | `32X INIT FAILED - STOPPED` / `68K EXC Vnn FRAME:` | row 19 was negative; or **the 68000 crashed** (main.c `HW1`): `V02` bus error, `V03` address error, `V04` illegal instruction, `V05` divide by zero, `V06` CHK, `V07` TRAPV, `V08` privilege, `V09` trace, `V0A` line A, `V0B` line F (hex). **This one shows on any disc:** the trap turns the Genesis display on itself (`hw_md.s` K29). |
| 22 | the exception frame | V02/V03: `FC ADDR-HI ADDR-LO IR SR PC-HI PC-LO`; others: `SR PC-HI PC-LO` + stack. The PC leads to the routine in `hw_md.s` (the listener sits at `0xFF1000`). |
| 23–24 | `BRAM ID:nn (nKB) OLD:nn`, `OLD:xxxx VIA 400000:xxxx`, `WROTE 5AA5 VIA 900000:xxxx` | the backup RAM size; the cart probe: what the 68000 read back through each window |
| 25 | `SAFE CART: FOUND` / `NOT FOUND` | **is there a RAM cart?** (the probe of rows 23–24) |
| 26 | `(TEXT BLINKS TO SHOW SH2 IS ALIVE)` | |
| 27 | `CDDA: PLAY TRACK n (REPEAT)`, `CDDA Tn ST ss TRY n` | CD audio requests and the drive's answer |

**The white lines** (`Activated 32X`, `Cleared 32X`, …) come from Chilly Willy's 32X start-up in `hw_md.s`
(`INIT_32X`), at column 12 of rows 19–26. Green rows 19, 20 and 23–26 are written over them, so usually only
rows 21–22 (`Activated 32X`, `Cleared 32X`) stay readable. If the 32X start-up stops, its last white line
is the step that did not finish.

**Speed on the diagnostic disc.** Until COMM12 reads `4B10`, the Sub-CPU keeps redrawing its status rows
(row 10 when the status changes, row 19 every 64 passes). With an error code in COMM12 the game therefore
runs slower on the diagnostic disc. That is expected: the release disc compiles that text out.

**After a file is not found** (row 12 `ERR: … NOT FOUND`), `scd.c` lists the disc's root directory on rows
19–25 as `FILE: <name>`, over the rows that were there (including row 25 `SAFE CART`); the heartbeat soon
rewrites row 19. Compare the list with the name you asked for.

## 6. Codes from the other games (so you do not reuse them by accident)

| Codes | Game | Meaning |
|---|---|---|
| `4B01`–`4B08`, `4B51`–`4B54`, `4B6n`, `4B7n` | Kobo Deluxe | start-up steps (pack, graphics, sound, backup RAM, tiles, music) |
| `4BE1`–`4BE3`, `4BE5`, `4BE6` | Kobo Deluxe | pack not found, chunk failed, bad pack signature, too many banks, program too big |
| `4Fxx`, `4E8n`, `4E14` | OpenLara | level xx loading, chunk n, level ready |
| `4B10` | all | running |

Pick your game's own range (hello uses `4E00`–`4E01` and `4EE1`–`4EE4`). Keep `4B10` for "running":
the Sub-CPU stops redrawing its text when it sees it.
