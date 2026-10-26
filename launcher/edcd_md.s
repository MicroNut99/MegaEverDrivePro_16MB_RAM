| edcd_md.s - EDCD.32X's 68000 part: a CD32X disc started from a cartridge (docs/12_CARTRIDGE_BOOT.md).
|
| The Mega EverDrive PRO cannot let go of the cartridge line (console, 25 Oct 2026: EDPROBE), so the console
| stays in cartridge mode ("mode 1"): the EverDrive's 16 MB at 0x000000, the Sega CD at 0x400000.  This
| ROM does what the Sega CD BIOS and the disc's boot block do in a CD boot, then hands over to the same
| stack, built with MODE1=1 (its listener knows the cartridge-boot map):
|   1. the EverDrive's mapper as a 4 MB RAM cart (banks 0-7 in windows 0-7, writes on);
|   2. the RAM jump table a CD boot's BIOS leaves at 0xFFFD00 (the listener puts its vblank there);
|   3. the Sub-CPU: reset, its BIOS unpacked from the Sega CD's ROM into PRG-RAM, the start-up program
|      (edcd_sp.s) at 0x6000 and the stack's Sub-CPU program at 0x8000, then run (Chilly Willy's mode 1
|      InitCD);
|   4. the disc boot block's main-CPU program (US_BOOT.BIN after its security code): the handshake
|      'I' / 'B' / 'M', then into Word RAM - at 0x600000 here - where the listener starts.
| The 32X was started before this ran: the cartridge's 32X header (0x3C0) has the boot ROMs load the
| stack's SH2 loader (CDLOADER), which waits for the listener, as in a CD boot.
| If it stops, the screen colour says where: CYAN the 32X did not start the SH2 loader, BLUE no Sega CD,
| MAGENTA no Sub-CPU BIOS found in the Sega CD's ROM, RED the Sub-CPU did not start, YELLOW the Sub-CPU
| program did not hand over.
| Runs at 0x880800 (cart 0x800 through the 32X window).

| ED_W0 (build_edcd.sh: EDCD_W0=<bank>, default 0): the EverDrive bank in window 0 once EDCD hands over.
| 0 = bank 0, where the EverDrive loaded this ROM.  Not 0: EDCD's first 2 KB (its 68000 jump table, the
| vblank's way to the listener) go to that bank and window 0 becomes it, so the game is copied there
| (edtest never wrote bank 0: its program was in it).
        .ifndef ED_W0
        .equ    ED_W0, 0
        .endif
        .ifndef ED_CTRL0
        .equ    ED_CTRL0, 0x8000
        .endif

        .text
        .global _start
_start:
        move.w  #0x2700,sr
        lea     0xFFFD00,sp
        move.b  #0,0xA15107             /* RV off: the SH2 boot ROMs read this cart */

| 0. first the 32X: its boot ROMs load the stack's SH2 loader from this cart (the 32X header at 0x3C0),
|    then write M_OK / S_OK.  Nothing may touch RV or the mapper before that (console, 25 Oct 2026:
|    RV = 1 for the mapper writes while they loaded it -> the SH2s never started).  edtest also writes
|    the mapper only once the SH2s run.
        move.l  #1000000,d2             /* about 6 s */
20:     cmpi.l  #0x4D5F4F4B,0xA15120    /* M_OK */
        bne.b   21f
        cmpi.l  #0x535F4F4B,0xA15124    /* S_OK */
        beq.b   22f
21:     subq.l  #1,d2
        bne.b   20b
        move.w  #0x0EE0,d1              /* CYAN: the 32X did not start the SH2 loader */
        bra     fail
22:

| 1. the RAM cart: windows 1-7 = banks 1-7, then writes on (window 0 = bank 0 = this ROM).
|    With the 32X on, a write to the cart's registers (0xA130xx) only reaches the cart while RV = 1
|    (console, 25 Oct 2026: without it the EverDrive stayed write-protected and the game's copy did not
|    stick, STATE 4DE2; edtest sets RV for them, as Chilly Willy's SRAM code does).  RV = 1 also takes
|    the cart away from 0x880000, where this runs, so the writes are made from work RAM.
        lea     ed_regs(pc),a0
        lea     0xFF8000,a1
        move.w  #(ed_regs_end-ed_regs)/2-1,d0
1:      move.w  (a0)+,(a1)+
        dbra    d0,1b
        jsr     0xFF8000

| 2. the BIOS's RAM jump table: 29 entries "jmp 0xFFFDAE" at 0xFFFD00-0xFFFDAD, an rte at 0xFFFDAE.
|    The vectors (32X vector ROM -> this cart's table at 0x200 -> here) are those of a CD boot:
|    vblank 0xFFFD06, hblank 0xFFFD0C, level 2 0xFFFD12, traps 0xFFFD18-, errors 0xFFFD78-0xFFFDA2.
        lea     0xFFFD00,a0
        moveq   #28,d1
2:      move.w  #0x4EF9,(a0)+
        move.l  #0xFFFDAE,(a0)+
        dbra    d1,2b
        move.w  #0x4E73,(a0)            /* rte */

| 3. the Sega CD                      (progress in the 32X's COMM12, as CDLOADER's STATE codes: ED01-ED05)
        move.w  #0xED01,0xA1512C
        cmpi.l  #0x53454741,0x400100    /* "SEGA": its BIOS at 0x400000 */
        beq.b   3f
        move.w  #0x0E00,d1              /* BLUE */
        bra     fail
3:
        lea     bios_list(pc),a2
4:      move.l  (a2)+,d0
        beq.b   5f
        movea.l d0,a0
        bsr     is_subbios
        beq.b   7f
        bra.b   4b
5:      lea     0x410000,a0             /* not at a known place: look for it */
6:      bsr     is_subbios
        beq.b   7f
        lea     0x100(a0),a0
        cmpa.l  #0x420000,a0
        blo.b   6b
        move.w  #0x0E0E,d1              /* MAGENTA */
        bra     fail
7:      move.l  a0,d7                   /* d7 = the packed Sub-CPU BIOS */
        move.w  #0xED02,0xA1512C

        move.w  #0xFF00,0xA12002
        move.b  #0x03,0xA12001
        move.b  #0x02,0xA12001
        move.b  #0x00,0xA12001
8:      move.b  #0x02,0xA12001          /* bus request, until granted */
        btst    #1,0xA12001
        beq.b   8b
        move.w  #0x0002,0xA12002

        lea     0x420000,a0             /* PRG-RAM 0x00000-0x1FFFF */
        move.w  #0x20000/4-1,d0
9:      clr.l   (a0)+
        dbra    d0,9b

        pea     0x420000
        move.l  d7,-(sp)
        jsr     Kos_Decomp
        addq.l  #8,sp

        lea     sp_bin,a0               /* the start-up program -> 0x6000 */
        lea     0x426000,a1
        move.w  #(sp_bin_end-sp_bin)/2-1,d0
10:     move.w  (a0)+,(a1)+
        dbra    d0,10b

        lea     app_bin,a0              /* the stack's Sub-CPU program -> 0x8000 */
        lea     0x428000,a1
        move.w  #(app_bin_end-app_bin)/2-1,d0
11:     move.w  (a0)+,(a1)+
        dbra    d0,11b

        move.b  #0x00,0xA1200E
        move.b  #0x2A,0xA12002          /* write protect 0x0000-0x53FF (its BIOS) */
12:     move.b  #0x01,0xA12001          /* run */
        btst    #0,0xA12001
        beq.b   12b

| 4. the disc boot block's main-CPU program
        move.w  #0xED03,0xA1512C
        lea     0xA10000,a5
        move.b  #0,0x200E(a5)
        move.w  #600,d2                 /* 10 s of frames */
13:     cmpi.b  #'I,0x200F(a5)          /* the Sub-CPU's start-up program has run */
        beq.b   14f
        bsr     frame
        dbra    d2,13b
        move.w  #0x000E,d1              /* RED */
        bra     fail
14:     move.w  #0xED04,0xA1512C
        bset    #1,0x2003(a5)           /* Word RAM to the Sub-CPU */
        move.b  #'B,0x200E(a5)
        move.w  #1800,d2                /* 30 s: the stack starts the drive first */
15:     cmpi.b  #'M,0x200F(a5)          /* the listener is in Word RAM */
        beq.b   16f
        bsr     frame
        dbra    d2,15b
        move.w  #0x00EE,d1              /* YELLOW */
        bra     fail
        move.w  #0xED05,0xA1512C
16:     btst    #0,0x2003(a5)           /* Word RAM back to the main CPU */
        beq.b   16b
        move.w  #0x2700,sr
        lea     0xFFFD00,sp
        .if ED_W0
        lea     w0_move(pc),a0          /* from work RAM: window 0 changes under this code */
        lea     0xFF8100,a1
        move.w  #(w0_move_end-w0_move)/2-1,d0
17:     move.w  (a0)+,(a1)+
        dbra    d0,17b
        jmp     0xFF8100
        .else
        jmp     0x600000
        .endif

        .if ED_W0
| run at 0xFF8100: EDCD's 0x100-0x7FF to bank ED_W0 (through window 1 for a moment), the standard 32X
| vectors in front, then window 0 = that bank; all with RV = 1 (cart and registers), in words, as edtest
w0_move:
        move.b  #1,0xA15107
        move.w  #ED_W0,0xA130F2         /* window 1 = the new bank */
        move.w  #ED_CTRL0|0x2000,0xA130F0 /* W on for this copy only (no other 68000 write until off) */
        lea     0x000100,a0
        lea     0x080100,a1
        move.w  #(0x800-0x100)/2-1,d0
1:      move.w  (a0)+,(a1)+
        dbra    d0,1b
        lea     0x080000,a1
        move.l  #0x01000000,(a1)+
        moveq   #62,d0
2:      move.l  #0x000003F0,(a1)+
        dbra    d0,2b
        move.w  #1,0xA130F2             /* window 1 = bank 1 again */
        move.w  #ED_CTRL0+ED_W0,0xA130F0 /* window 0 = the new bank, W off again */
        move.b  #0,0xA15107
        jmp     0x600000
w0_move_end:
        .endif

| one frame (about 1/60 s of 68000 time - the VDP's blank flag is stuck at 1 while the display is off),
| then the Sub-CPU's level 2 interrupt: what a CD boot's BIOS (and the stack's listener) does every
| vblank.  The Sub-CPU BIOS counts on it.
frame:
        move.w  #12700,d0               /* x 10 cycles = 1/60 s at 7.67 MHz */
1:      dbra    d0,1b
        move.w  0xA12000,d0
        ori.w   #0x0100,d0              /* IFL2 */
        move.w  d0,0xA12000
        rts

| a0 = a place in the Sega CD's ROM: Z = 1 if the packed Sub-CPU BIOS starts there ("SEGA" or "WONDER"
| at +0x6D, an odd address: byte compares)
is_subbios:
        lea     0x6D(a0),a1
        lea     t_sega(pc),a3
        moveq   #3,d0
1:      cmpm.b  (a1)+,(a3)+
        dbne    d0,1b
        beq.b   9f
        lea     0x6D(a0),a1
        lea     t_wonder(pc),a3
        moveq   #5,d0
2:      cmpm.b  (a1)+,(a3)+
        dbne    d0,2b
9:      rts

| copied to 0xFF8000 and run there (position independent): the EverDrive's mapper, with RV = 1
ed_regs:
        move.b  #1,0xA15107             /* RV on: the register writes reach the cart */
        lea     0xA130F2,a0
        moveq   #1,d0
1:      move.w  d0,(a0)+                /* CTRL1-7 = banks 1-7 */
        addq.w  #1,d0
        cmpi.w  #8,d0
        bne.b   1b
        move.w  #ED_CTRL0,0xA130F0      /* CTRL0: P (0x8000; P + X with EDCD_X=1).  W stays OFF: console,
                                           25 Oct 2026 (edw0) - with W on, the 68000's writes elsewhere
                                           corrupt the cart and the SH2's reads of it.  The listener
                                           turns W on only inside a copy into the cart. */
        move.b  #0,0xA15107             /* RV off: the cart back at 0x880000, the SH2s may read it */
        rts
ed_regs_end:

| d1 = the colour: the whole screen in it, then stop
fail:
        lea     0xC00004,a1
        tst.w   (a1)
        move.w  #0x8004,(a1)
        move.w  #0x8104,(a1)            /* display off: the backdrop fills the screen */
        move.w  #0x8700,(a1)
        move.l  #0xC0000000,(a1)
        move.w  d1,0xC00000
1:      bra.b   1b

        .align  2
bios_list:
        .long   0x415800, 0x416000, 0x41AD00, 0
t_sega:
        .ascii  "SEGA"
t_wonder:
        .ascii  "WONDER"

        .align  2
sp_bin:
        .incbin "build/edcd_sp.bin"
        .align  2
sp_bin_end:
app_bin:
        .incbin "build/app_mode1.bin"
        .align  2
app_bin_end:

| the stack's SH2 loader, without its 56-byte CD boot table; the 32X header at 0x3C0 points here
        .align  8
        .global cdl_code
cdl_code:
        .incbin "build/cdloader_code.bin"
        .align  4
        .global cdl_code_end
cdl_code_end:
