# 06 – Hardware rules

Each rule below was learned on a real console. Every one of them has cost at least one dead disc in one
of the games (the game that taught it is named). Emulation forgives most of them, so pass these before
you burn.

## The boot stack

1. **Do not change the boot stack.** That is the boot block, `stack/subcpu/` (except `game_files.h`),
   `hw_md.s`, `stack/sh2/`, and your `crt0.s`'s start-up code. The diagnostic disc's lines are swapped in by
   `DIAG=1` for one build only (`tools/diag_switch.py`): never edit them by hand.
   - **Why:** Hurrican edited its boot path once and black-screened. Every team now freezes its boot
     stack, and SuperTux checks it with md5s.
   - **The check:** `tools/check_boot.py`. Your game code starts at `main()`.
2. **New Sub-CPU commands go behind a build switch** in `main.c`, compiled with HWFLAGS.
   - Take a number nobody uses (see [03](03_COMMANDS.md)).
   - Record it in the command ledger.
   - Test it in ares, then on the console, before anything depends on it.
   - Then run `check_boot.py --update` on purpose.
3. **The 68000 reads its command flag twice (HW10).**
   - **Why:** a torn read once started listener routine 2 with a garbage argument (`SR = A71E`, exception
     V09) and hung the console. Routine 2 is disabled since.
   - **Rule:** any listener routine you add must be harmless if it is started by mistake.
4. **Never hand the 68000 to the ROM** (CMD 60). It must stay the listener, or every CD service stops.

## COMM registers

5. **One writer per register.**

   | Register | Who writes it |
   |---|---|
   | COMM0 | the SH2 sets it; the Sub-CPU clears it |
   | COMM14 | the Sub-CPU only (heartbeat) |
   | COMM6 | the game only (OpenLara's renderer sync) |
   | COMM4 | master ↔ slave only |

   - Write COMM12 as 16 bits. A 32-bit write also writes COMM14.
   - **Why:** in OpenLara, anything else that wrote COMM6 froze the game.
6. **The SH2 never polls a register the 68000 writes.** Ask with a command instead (the pad is CMD 48).
   (OpenJazz, Hurrican)
7. **Pause between polls of COMM0** and put a timeout on every wait. A tight poll steals bus time from
   the 68000's copy; a missing timeout turns any lost command into a hang.
8. **Only the master SH2 sends commands.** The slave asks the master. (Hurrican queues music requests
   while a slave job runs.)

## The RAM cart

9. **The SH2s cannot write the cart.** Every write goes through the 68000 with RV = 1 (CMD 46, 61).
10. **Purge the caches after the 68000 writes the cart,** on both SH2s (CCR = `0x11`), or read
    cache-through (`0x22xxxxxx`).
11. **While RV = 1, SH2 cart reads simply wait** - on a real cartridge (the 16 MB PCB cart, every CD
    boot so far). No tricks are needed there; in OpenLara, parking the slave in SDRAM during loads once
    caused many dead discs. **Not on the Mega EverDrive PRO**: there an SH2 cart access during RV = 1 is
    answered at once with garbage (`edsave3`, 26 Oct 2026, docs/12), so with the EverDrive both SH2s must
    be in SDRAM code with interrupts masked for every 68000 copy with RV = 1 (OpenLara EDSAVE-4, the
    `EDCD_SAVE=1` disc only).
12. **Keep code that runs during a load out of the cart.** OpenLara's movies play while they stream:
    their hot loops are in SDRAM.
13. **Put packs on 32 KB boundaries** (one CMD 46 chunk), and keep the first 512 KB for your ROM.
    (Hurrican, OpenJazz)
14. **The save area is shared by every game.** Take a piece from the ledger and never give it a second
    meaning ([04](04_MEMORY_MAPS.md)).

## CD

15. **No CD data reads while CD audio plays.** Stop the music (CMD 54) before every load (OpenLara, all of
    them). The drive cannot do both. The load fails or hangs.
16. **Never restart a CD track every frame.** It stays silent while the drive spins up again.
    (OpenLara)
17. **Open the file again before CMD 46, and write its length into COMM8 for every chunk.** CMD 46 reads
    from the file the **last** CMD 38 opened (the Sub-CPU keeps its position and length). It takes the
    length from COMM8, which other commands' replies overwrite. So after the boot picture (CMD 38 file 0
    + CMD 51), reopen your file. (OpenJazz)
18. **Word RAM in 2M mode: the Sub-CPU must not swap banks.** Its next Word RAM access hangs. (Sonic CD
    2011)

## The 32X picture

19. **16-bit writes to the frame buffer.** Byte writes may not land. (OpenJazz)
20. **Colour `0x0000` is see-through:** the Genesis layer shows through. Use `0x0400` for black, or
    switch the Genesis display off (CMD 52, COMM2 = 0). (OpenJazz, OpenLara)
21. **The Genesis text sits on top of the 32X picture.** Leave it off once your game runs. Diagnostic text
    costs time even when hidden, so it stays compiled out in a release. (OpenLara)

## The Genesis VDP (if you add plane commands)

22. **The SH2 decides; the 68000 only copies.** The 68000 does VDP work in vblank, with interrupts off
    around VDP command writes.
23. **The vblank handler never reads the cart.** Load from the cart outside vblank, through the
    `0x900000` window. (OpenJazz, SuperTux, Sonic)

## SH2 code

24. **Code in SDRAM:** build it with `-fno-align-functions` / `-fno-align-labels` (SuperTux) and check
    that it fits (`check_sdram_code.py` in SuperTux).
25. **Give the slave its own stack** if it does real work. The slave stack in `crt0.s` is only 2 KB.
    (Hurrican)
26. **newlib's `malloc` is not safe on both SH2s at once.** (OpenJazz)
