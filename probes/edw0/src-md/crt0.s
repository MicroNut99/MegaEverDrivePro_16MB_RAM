| SEGA 32X support code for the 68000
| by Chilly Willy

        .text

| 0x880800 - entry point for reset/cold-start

        .global _start
_start:

| Clear Work RAM
        moveq   #0,d0
        move.w  #0x3FFF,d1
        suba.l  a1,a1
1:
        move.l  d0,-(a1)
        dbra    d1,1b

| Copy initialized variables from ROM to Work RAM
        lea     __text_end,a0
        move.w  #__data_size,d0
        lsr.w   #1,d0
        subq.w  #1,d0
2:
        move.w  (a0)+,(a1)+
        dbra    d0,2b

        lea     __stack,sp              /* set stack pointer to top of Work RAM */

        bsr     init_hardware           /* initialize the console hardware */

        jsr     main                    /* call program main() */
3:
        stop    #0x2700
        bra.b   3b

        .align  64

| 0x880840 - 68000 General exception handler

        move.l  d0,-(sp)
        move.l  4(sp),d0            /* jump table return address */
        sub.w   #0x206,d0           /* 0 = BusError, 6 = AddrError, etc */

        /* handle exception here */

        move.l  (sp)+,d0
        addq.l  #4,sp               /* pop jump table return address */
        rte

        .align  64

| 0x880880 - 68000 Level 4 interrupt handler - HBlank IRQ

        rte

        .align  64

| 0x8808C0 - 68000 Level 6 interrupt handler - VBlank IRQ

        move.l  d0,-(sp)
        move.l  vblank,d0
        beq.b   1f
        move.l  a0,-(sp)
        movea.l d0,a0
        jmp     (a0)
1:
        move.l  (sp)+,d0
        rte


| Initialize the MD side to a known state for the game

init_hardware:
        lea     0xC00004,a0
        move.w  #0x8104,(a0)            /* display off, vblank disabled */
        move.w  (a0),d0                 /* read VDP Status reg */

| init joyports
        move.b  #0x40,0xA10009
        move.b  #0x40,0xA1000B
        move.b  #0x40,0xA10003
        move.b  #0x40,0xA10005

| init MD VDP
        move.w  #0x8004,(a0) /* reg. 0 - Disable HBL INT */
        move.w  #0x8174,(a0) /* reg. 1 - Enable display, VBL INT, DMA + 28 VCell size */
        move.w  #0x8230,(a0) /* reg. 2 - Plane A =$30*$400=$C000 */
        move.w  #0x832C,(a0) /* reg. 3 - Window  =$2C*$400=$B000 */
        move.w  #0x8407,(a0) /* reg. 4 - Plane B =$7*$2000=$E000 */
        move.w  #0x855E,(a0) /* reg. 5 - sprite table begins at $BC00=$5E*$200 */
        move.w  #0x8600,(a0) /* reg. 6 - not used */
        move.w  #0x8700,(a0) /* reg. 7 - Background Color number*/
        move.w  #0x8800,(a0) /* reg. 8 - not used */
        move.w  #0x8900,(a0) /* reg. 9 - not used */
        move.w  #0x8A01,(a0) /* reg 10 - HInterrupt timing */
        move.w  #0x8B00,(a0) /* reg 11 - $0000abcd a=extr.int b=vscr cd=hscr */
        move.w  #0x8C81,(a0) /* reg 12 - hcell mode + shadow/highight + interlaced mode (40 cell, no shadow, no interlace)*/
        move.w  #0x8D2E,(a0) /* reg 13 - HScroll Table = $B800 */
        move.w  #0x8E00,(a0) /* reg 14 - not used */
        move.w  #0x8F02,(a0) /* reg 15 - auto increment data */
        move.w  #0x9011,(a0) /* reg 16 - scrl screen v&h size (64x64) */
        move.w  #0x9100,(a0) /* reg 17 - window hpos */
        move.w  #0x92FF,(a0) /* reg 18 - window vpos */

        move.w  #0,0xA15128         /* controller 1 */
        move.w  #0,0xA1512A         /* controller 2 */
| look for mouse
        lea     0xA10003,a0
0:
        jsr     get_mky
        cmpi.l  #-2,d0
        beq.b   0b                  /* timeout */
        cmpi.l  #-1,d0
        beq.b   1f                  /* no mouse */
        move.w  #0xF001,0xA15128    /* mouse in port 1 */
1:
        lea     2(a0),a0
2:
        jsr     get_mky
        cmpi.l  #-2,d0
        beq.b   2b                  /* timeout */
        cmpi.l  #-1,d0
        beq.b   3f                  /* no mouse */
        move.w  #0xF001,0xA1512A    /* mouse in port 2 */
3:

| allow the 68k to access the FM chip
        move.w  #0x0100,0xA11100    /* Z80 assert bus request */
        move.w  #0x0100,0xA11200    /* Z80 deassert reset */

| wait on Mars side
        move.b  #0,0xA15107             /* clear RV - allow SH2 to access ROM */
0:
        cmp.l   #0x4D5F4F4B,0xA15120    /* M_OK */
        bne.b   0b                      /* wait for master ok */
1:
        cmp.l   #0x535F4F4B,0xA15124    /* S_OK */
        bne.b   1b                      /* wait for slave ok */

        move.l  #vert_blank,vblank  /* set vertical blank interrupt handler */
        move.w  #0x2000,sr          /* enable interrupts */
        rts


| void write_byte(void *dst, unsigned char val)
        .global write_byte
write_byte:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        move.b  d0,(a0)
        rts

| void write_word(void *dst, unsigned short val)
        .global write_word
write_word:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        move.w  d0,(a0)
        rts

| void write_long(void *dst, unsigned int val)
        .global write_long
write_long:
        movea.l 4(sp),a0
        move.l  8(sp),d0
        move.l  d0,(a0)
        rts

| unsigned char read_byte(void *src)
        .global read_byte
read_byte:
        movea.l 4(sp),a0
        move.b  (a0),d0
        rts

| unsigned short read_word(void *src)
        .global read_word
read_word:
        movea.l 4(sp),a0
        move.w  (a0),d0
        rts

| unsigned int read_long(void *src)
        .global read_long
read_long:
        movea.l 4(sp),a0
        move.l  (a0),d0
        rts


        .data

| Put remaining code in data section to lower bus contention for the rom.

        .global do_main
do_main:
| (edtest: no write to 0xA130F1 here - on the EverDrive that is the low byte of its mapper register CTRL0)
| edtest: interrupts stay OFF for the whole test.  The vertical blank handler (vert_blank) uses d0/a0
| without saving them and writes the pads into COMM8/COMM10 - the registers that carry this test's replies.
        move.w  #0x2700,sr

        move.w  0xA15100,d0
        or.w    #0x8000,d0
        move.w  d0,0xA15100         /* set FM - allow SH2 access to MARS hw */
        move.l  #0,0xA15120         /* let Master SH2 run */

main_loop:
        move.w  0xA15120,d0         /* get COMM0 */
        bne.b   handle_req

        nop
        bra     main_loop

| process request from Master SH2
handle_req:
        cmpi.w  #0x01FF,d0
        bls     read_sram
        cmpi.w  #0x02FF,d0
        bls     write_sram
        cmpi.w  #0x03FF,d0
        bls     start_music
        cmpi.w  #0x04FF,d0
        bls     stop_music
        cmpi.w  #0x05FF,d0
        bls     read_mouse
        cmpi.w  #0x06FF,d0
        bls     ed_mapper
        cmpi.w  #0x07FF,d0
        bls     ed_fill
        cmpi.w  #0x08FF,d0
        bls     ed_verify
        cmpi.w  #0x09FF,d0
        bls     ed_fill_hi
        cmpi.w  #0x0BFF,d0
        bls     ed_hammer
        cmpi.w  #0x0CFF,d0
        bls     ed_hammer_cd
        cmpi.w  #0x0DFF,d0
        bls     ed_hammer_vdp
        cmpi.w  #0x0EFF,d0
        bls     ed_hammer_wram
        cmpi.w  #0x0FFF,d0
        bls     ed_vint_on
        cmpi.w  #0x10FF,d0
        bls     ed_write_vdp
        cmpi.w  #0x11FF,d0
        bls     ed_write_wram
        cmpi.w  #0x12FF,d0
        bls     ed_write_gate
| unknown command
        move.w  #0,0xA15120         /* done */
        bra     main_loop

| ---- edtest: the EverDrive extended SSF mapper as RAM -------------------------------------------
| 0x06nn: mapper register nn (0 = CTRL0 0xA130F0, 1..7 = CTRL1..7 0xA130F2..FE) = COMM2
ed_mapper:
        moveq   #0,d1
        move.b  d0,d1
        add.w   d1,d1
        lea     0xA130F0,a0
        move.w  0xA15122,d2         /* the value */
        move.w  #0x2700,sr
        move.b  #1,0xA15107         /* set RV, as Chilly's SRAM code does for 0xA130F1 */
        move.w  d2,0(a0,d1.w)
        move.b  #0,0xA15107         /* clear RV */
        move.w  #0x2700,sr         /* (interrupts stay off: see do_main) */
        move.w  #0,0xA15120         /* done */
        bra     main_loop

| the pattern of a 512 KB window, word i (0..262143): (i & 0xFFFF) ^ ((i >> 16) << 12) ^ seed
| 0x07nn: fill window nn (1..7 = cart 0x080000 x nn, through CTRLnn) with the pattern, COMM2 = seed
ed_fill:
        bsr     ed_setup
2:      move.w  d6,d1
        eor.w   d5,d1
        move.w  d1,(a0)+
        addq.w  #1,d6
        dbra    d7,2b
        addq.w  #1,d4
        cmpi.w  #4,d4
        beq.b   3f
        bsr     ed_quarter
        bra.b   2b
3:      move.b  #0,0xA15107         /* clear RV */
        move.w  #0x2700,sr         /* (interrupts stay off: see do_main) */
        move.w  #0,0xA15120         /* done */
        bra     main_loop

| 0x09nn: fill quarters 2 and 3 of window nn (0x40000-0x7FFFF) with the pattern, COMM2 = seed:
| edw0 - bank 0's upper half, above this program, through window 1
ed_fill_hi:
        bsr     ed_setup
        adda.l  #0x40000,a0
        moveq   #2,d4
        bsr     ed_quarter
2:      move.w  d6,d1
        eor.w   d5,d1
        move.w  d1,(a0)+
        addq.w  #1,d6
        dbra    d7,2b
        addq.w  #1,d4
        cmpi.w  #4,d4
        beq.b   3f
        bsr     ed_quarter
        bra.b   2b
3:      move.b  #0,0xA15107         /* clear RV */
        move.w  #0x2700,sr
        move.w  #0,0xA15120         /* done */
        bra     main_loop

| 0x0B00: edw0 - read window 0 through the 32X's fixed window (0x880200, where the 68000's vectors
| send it), COMM2 x 1000 times 8 words, RV = 0 - the 68000 busy on the cart while the SH2 reads it
ed_hammer:
        moveq   #0,d1
        move.w  0xA15122,d1
        mulu.w  #1000,d1
        lea     0x880200,a0
1:      move.w  (a0),d0
        move.w  2(a0),d0
        move.w  4(a0),d0
        move.w  6(a0),d0
        move.w  8(a0),d0
        move.w  10(a0),d0
        move.w  12(a0),d0
        move.w  14(a0),d0
        subq.l  #1,d1
        bne.b   1b
        move.w  #0,0xA15120         /* done */
        bra     main_loop

| 0x0C00: edw0 - the 68000 reads the Sega CD's side of the bus (the BIOS ROM at 0x400000, as the
| cartridge boot has it), COMM2 x 1000 times 8 words, while the SH2 reads the cart
ed_hammer_cd:
        moveq   #0,d1
        move.w  0xA15122,d1
        mulu.w  #1000,d1
        lea     0x400100,a0
        bra.b   ed_hammer_loop
| 0x0D00: edw0 - the 68000 reads the Genesis VDP's status port meanwhile
ed_hammer_vdp:
        moveq   #0,d1
        move.w  0xA15122,d1
        mulu.w  #1000,d1
        lea     0xC00004,a0
1:      move.w  (a0),d0
        move.w  (a0),d0
        move.w  (a0),d0
        move.w  (a0),d0
        move.w  (a0),d0
        move.w  (a0),d0
        move.w  (a0),d0
        move.w  (a0),d0
        subq.l  #1,d1
        bne.b   1b
        move.w  #0,0xA15120         /* done */
        bra     main_loop
| 0x0E00: edw0 - the 68000 reads its work RAM meanwhile
ed_hammer_wram:
        moveq   #0,d1
        move.w  0xA15122,d1
        mulu.w  #1000,d1
        lea     0xFF0100,a0
ed_hammer_loop:
1:      move.w  (a0),d0
        move.w  2(a0),d0
        move.w  4(a0),d0
        move.w  6(a0),d0
        move.w  8(a0),d0
        move.w  10(a0),d0
        move.w  12(a0),d0
        move.w  14(a0),d0
        subq.l  #1,d1
        bne.b   1b
        move.w  #0,0xA15120         /* done */
        bra     main_loop

| 0x0F00: edw0 - the 68000 takes its vertical blank interrupt (vert_blank: pads) for about a second,
| as the cartridge boot's listener does every frame: the vector comes through the 32X's vector shadow
ed_vint_on:
        moveq   #0,d1
        move.w  0xA15122,d1
        mulu.w  #4000,d1
        move.w  #0x2000,sr
1:      nop
        subq.l  #1,d1
        bne.b   1b
        move.w  #0x2700,sr
        move.w  #0,0xA15120         /* done */
        bra     main_loop
| 0x1000: edw0 - the 68000 writes the VDP's control port (register 15 = 2, its value already)
ed_write_vdp:
        moveq   #0,d1
        move.w  0xA15122,d1
        mulu.w  #1000,d1
        lea     0xC00004,a0
        move.w  #0x8F02,d0
        bra.b   ed_write_loop
| 0x1100: edw0 - the 68000 writes its work RAM
ed_write_wram:
        moveq   #0,d1
        move.w  0xA15122,d1
        mulu.w  #1000,d1
        lea     0xFF0100,a0
        moveq   #0,d0
        bra.b   ed_write_loop
| 0x1200: edw0 - the 68000 writes the Sega CD gate array's main comm register 0 (0xA12010)
ed_write_gate:
        moveq   #0,d1
        move.w  0xA15122,d1
        mulu.w  #1000,d1
        lea     0xA12010,a0
        moveq   #0,d0
ed_write_loop:
1:      move.w  d0,(a0)
        move.w  d0,(a0)
        move.w  d0,(a0)
        move.w  d0,(a0)
        move.w  d0,(a0)
        move.w  d0,(a0)
        move.w  d0,(a0)
        move.w  d0,(a0)
        subq.l  #1,d1
        bne.b   1b
        move.w  #0,0xA15120         /* done */
        bra     main_loop

| 0x08nn: verify window nn against the pattern, COMM2 = seed.
| Reply: COMM8+10 = number of bad words, COMM12+14 = offset of the first (0xFFFFFFFF = none),
|        COMM2 = the word read there
ed_verify:
        bsr     ed_setup
        moveq   #0,d2               /* bad words */
        movea.l #-1,a1              /* first bad offset */
        suba.l  a2,a2               /* the word read there */
2:      move.w  d6,d1
        eor.w   d5,d1
        cmp.w   (a0)+,d1
        beq.b   4f
        addq.l  #1,d2
        cmpa.l  #-1,a1
        bne.b   4f
        lea     -2(a0),a1
        movea.w -2(a0),a2
4:      addq.w  #1,d6
        dbra    d7,2b
        addq.w  #1,d4
        cmpi.w  #4,d4
        beq.b   3f
        bsr     ed_quarter
        bra.b   2b
3:      move.b  #0,0xA15107         /* clear RV */
        move.w  #0x2700,sr         /* (interrupts stay off: see do_main) */
        move.l  d2,0xA15128
        move.l  a1,d1
        cmpi.l  #-1,d1
        beq.b   5f
        andi.l  #0x7FFFF,d1         /* offset within the window */
5:      move.l  d1,0xA1512C
        move.w  a2,0xA15122
        move.w  #0,0xA15120         /* done */
        bra     main_loop

| a0 = window nn (with RV = 1 the cart is at 0x000000), d3 = seed, d4 = quarter 0, interrupts off, RV = 1
ed_setup:
        moveq   #0,d1
        move.b  d0,d1
        beq.b   9f                  /* window 0 = this program: never */
        cmpi.b  #7,d1
        bhi.b   9f
        moveq   #19,d2
        lsl.l   d2,d1
        movea.l d1,a0
        move.w  0xA15122,d3
        moveq   #0,d4
        move.w  #0x2700,sr
        move.b  #1,0xA15107         /* set RV: the 68000 sees the cart at 0x000000 */
        bra.b   ed_quarter          /* d5 d6 d7 for quarter 0, then back to the caller (fill or verify) */
9:      addq.l  #4,sp               /* a bad window number: drop the caller's return, answer "done" */
        move.w  #0,0xA15120
        bra     main_loop
| d5 = seed ^ (quarter << 12), d6 = 0, d7 = 65535 (one quarter = 65536 words)
ed_quarter:
        move.w  d4,d5
        lsl.w   #8,d5
        lsl.w   #4,d5
        eor.w   d3,d5
        moveq   #0,d6
        move.w  #0xFFFF,d7
        rts

read_sram:
        move.w  #0x2700,sr          /* disable ints */
        moveq   #0,d1
        moveq   #0,d0
        move.w  0xA15122,d0         /* COMM2 holds offset */
        lea     0x200000,a0
        move.b  #1,0xA15107         /* set RV */
        move.b  #3,0xA130F1         /* SRAM enabled, write protected */
        move.b  1(a0,d0.l),d1       /* read SRAM */
        move.b  #2,0xA130F1         /* SRAM disabled, write protected */
        move.b  #0,0xA15107         /* clear RV */
        move.w  d1,0xA15122         /* COMM2 holds return byte */
        move.w  #0,0xA15120         /* done */
        move.w  #0x2000,sr          /* enable ints */
        bra     main_loop

write_sram:
        move.w  #0x2700,sr          /* disable ints */
        moveq   #0,d1
        move.w  0xA15122,d1         /* COMM2 holds offset */
        lea     0x200000,a0
        move.b  #1,0xA15107         /* set RV */
        move.b  #1,0xA130F1         /* SRAM enabled, write enabled */
        move.b  d0,1(a0,d1.l)       /* write SRAM */
        move.b  #2,0xA130F1         /* SRAM disabled, write protected */
        move.b  #0,0xA15107         /* clear RV */
        move.w  #0,0xA15120         /* done */
        move.w  #0x2000,sr          /* enable ints */
        bra     main_loop

set_rom_bank:
        move.l  a0,d3
        swap    d3
        lsr.w   #4,d3
        andi.w  #3,d3
        move.w  d3,0xA15104         /* set ROM bank select */
        move.l  a0,d3
        andi.l  #0x0FFFFF,d3
        ori.l   #0x900000,d3
        movea.l d3,a1
        rts

start_music:
        tst.w   cd_ok
        beq.b   2f                  /* couldn't init cd */
        tst.b   cd_ok
        bne.b   0f                  /* disc found - try to play track */
        /* check for CD */
10:
        move.b  0xA1200F,d1
        bne.b   10b                 /* wait until Sub-CPU is ready to receive command */
        move.b  #'D,0xA1200E        /* set main comm port to GetDiskInfo command */
11:
        move.b  0xA1200F,d0
        beq.b   11b                 /* wait for acknowledge byte in sub comm port */
        move.b  #0x00,0xA1200E      /* acknowledge receipt of command result */

        cmpi.b  #'D,d0
        bne.b   2f                  /* couldn't get disk info */
        move.w  0xA12020,d0         /* BIOS status */
        cmpi.w  #0x1000,d0
        bhs.b   2f                  /* open, busy, or no disc */
        move.b  #1,cd_ok            /* we have a disc - try to play track */
0:
        move.b  0xA1200F,d1
        bne.b   0b                  /* wait until Sub-CPU is ready to receive command */

        move.b  d0,0xA12012         /* repeat flag */
        move.w  0xA15122,d0
        addq.w  #1,d0
        move.w  d0,0xA12010         /* track no. */
        move.b  #'P,0xA1200E        /* set main comm port to PlayTrack command */
1:
        move.b  0xA1200F,d0
        beq.b   1b                  /* wait for acknowledge byte in sub comm port */
        move.b  #0x00,0xA1200E      /* acknowledge receipt of command result */
2:
        move.w  #0,0xA15120         /* done */
        bra     main_loop

stop_music:
        tst.w   cd_ok
        beq.b   2f
0:
        move.b  0xA1200F,d1
        bne.b   0b                  /* wait until Sub-CPU is ready to receive command */

        move.b  #'S,0xA1200E        /* set main comm port to StopPlayback command */
1:
        move.b  0xA1200F,d0
        beq.b   1b                  /* wait for acknowledge byte in sub comm port */
        move.b  #0x00,0xA1200E      /* acknowledge receipt of command result */
2:
        move.w  #0,0xA15120         /* done */
        bra     main_loop

read_mouse:
        tst.b   d0
        bne.b   1f                  /* skip port 1 */

        move.w  0xA15128,d0
        andi.w  #0xF001,d0
        cmpi.w  #0xF001,d0
        bne.b   1f                  /* no mouse in port 1 */
        lea     0xA10003,a0
        bsr     get_mky
        bset    #31,d0
        move.w  d0,0xA15122
        swap    d0
        move.w  d0,0xA15120
0:
        move.w  0xA15120,d0
        bne.b   0b                  /* wait for SH2 to read mouse value */
        bra     main_loop
1:
        move.w  0xA1512A,d0
        andi.w  #0xF001,d0
        cmpi.w  #0xF001,d0
        bne.b   3f                  /* no mouse in port 2 */
        lea     0xA10005,a0
        bsr     get_mky
        bset    #31,d0
        move.w  d0,0xA15122
        swap    d0
        move.w  d0,0xA15120
2:
        move.w  0xA15120,d0
        bne.b   2b                  /* wait for SH2 to read mouse value */
        bra     main_loop
3:
        move.l  #-1,d0              /* no mouse */
        move.w  d0,0xA15122
        swap    d0
        move.w  d0,0xA15120
4:
        move.w  0xA15120,d0
        bne.b   4b                  /* wait for SH2 to read mouse value */
        bra     main_loop


vert_blank:
        move.l  d1,-(sp)
        move.l  d2,-(sp)

        /* read controllers */
        move.w  0xA15128,d0
        andi.w  #0xF000,d0
        cmpi.w  #0xF000,d0
        beq.b   0f                  /* no pad in port 1 (or mouse) */
        lea     0xA10003,a0
        bsr.b   get_pad
        move.w  d2,0xA15128         /* controller 1 current value */
0:
        move.w  0xA1512A,d0
        andi.w  #0xF000,d0
        cmpi.w  #0xF000,d0
        beq.b   1f                  /* no pad in port 2 (or mouse) */
        lea     0xA10005,a0
        bsr.b   get_pad
        move.w  d2,0xA1512A         /* controller 2 current value */
1:

        tst.w   gen_lvl2
        beq.b   2f
        lea     0xA12000,a0
        move.w  (a0),d0
        ori.w   #0x0100,d0
        move.w  d0,(a0)
2:
        move.l  (sp)+,d2
        move.l  (sp)+,d1
        movea.l (sp)+,a0
        move.l  (sp)+,d0
        rte

| get current pad value
| entry: a0 = pad control port
| exit:  d2 = pad value (0 0 0 1 M X Y Z S A C B R L D U) or (0 0 0 0 0 0 0 0 S A C B R L D U)
get_pad:
        bsr.b   get_input       /* - 0 s a 0 0 d u - 1 c b r l d u */
        move.w  d0,d1
        andi.w  #0x0C00,d0
        bne.b   no_pad
        bsr.b   get_input       /* - 0 s a 0 0 d u - 1 c b r l d u */
        bsr.b   get_input       /* - 0 s a 0 0 0 0 - 1 c b m x y z */
        move.w  d0,d2
        bsr.b   get_input       /* - 0 s a 1 1 1 1 - 1 c b r l d u */
        andi.w  #0x0F00,d0      /* 0 0 0 0 1 1 1 1 0 0 0 0 0 0 0 0 */
        cmpi.w  #0x0F00,d0
        beq.b   common          /* six button pad */
        move.w  #0x010F,d2      /* three button pad */
common:
        lsl.b   #4,d2           /* - 0 s a 0 0 0 0 m x y z 0 0 0 0 */
        lsl.w   #4,d2           /* 0 0 0 0 m x y z 0 0 0 0 0 0 0 0 */
        andi.w  #0x303F,d1      /* 0 0 s a 0 0 0 0 0 0 c b r l d u */
        move.b  d1,d2           /* 0 0 0 0 m x y z 0 0 c b r l d u */
        lsr.w   #6,d1           /* 0 0 0 0 0 0 0 0 s a 0 0 0 0 0 0 */
        or.w    d1,d2           /* 0 0 0 0 m x y z s a c b r l d u */
        eori.w  #0x1FFF,d2      /* 0 0 0 1 M X Y Z S A C B R L D U */
        rts

no_pad:
        move.w  #0xF000,d2
        rts

| read single phase from controller
get_input:
        move.b  #0x00,(a0)
        nop
        nop
        move.b  (a0),d0
        move.b  #0x40,(a0)
        lsl.w   #8,d0
        move.b  (a0),d0
        rts

| get current mouse value
| entry: a0 = mouse control port
| exit:  d0 = mouse value (0  0  0  0  0  0  0  0  YO XO YS XS S  M  R  L  X7 X6 X5 X4 X3 X2 X1 X0 Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0) or -2 (timeout) or -1 (no mouse)
get_mky:
        move.w  sr,d2
        move.w  #0x2700,sr      /* disable ints */

        move.b	#0x60,6(a0)     /* set direction bits */
        nop
        nop
        move.b  #0x60,(a0)      /* first phase of mouse packet */
        nop
        nop
0:
        btst    #4,(a0)
        beq.b   0b              /* wait on handshake */
        move.b  (a0),d0
        andi.b  #15,d0
        bne     mky_err         /* not 0 means not mouse */

        move.b  #0x20,(a0)      /* next phase */
        move.w  #254,d1         /* number retries before timeout */
1:
        btst    #4,(a0)
        bne.b   2f              /* handshake */
        dbra    d1,1b
        bra     timeout_err
2:
        move.b  (a0),d0
        andi.b  #15,d0
        move.b  #0,(a0)         /* next phase */
        cmpi.b  #11,d0
        bne     mky_err         /* not 11 means not mouse */
3:
        btst    #4,(a0)
        beq.b   4f              /* handshake */
        dbra    d1,3b
        bra     timeout_err
4:
        move.b  (a0),d0         /* specs say should be 15 */
        nop
        nop
        move.b  #0x20,(a0)      /* next phase */
        nop
        nop
5:
        btst    #4,(a0)
        bne.b   6f
        dbra    d1,5b
        bra     timeout_err
6:
        move.b  (a0),d0         /* specs say should be 15 */
        nop
        nop
        move.b  #0,(a0)         /* next phase */
        moveq   #0,d0           /* clear reg to hold packet */
        nop
7:
        btst    #4,(a0)
        beq.b   8f              /* handshake */
        dbra    d1,7b
        bra     timeout_err
8:
        move.b  (a0),d0         /* YO XO YS XS */
        move.b  #0x20,(a0)      /* next phase */
        lsl.w   #8,d0           /* save nibble */
9:
        btst    #4,(a0)
        bne.b   10f             /* handshake */
        dbra    d1,9b
        bra     timeout_err
10:
        move.b  (a0),d0         /* S  M  R  L */
        move.b  #0,(a0)         /* next phase */
        lsl.b   #4,d0           /* YO XO YS XS S  M  R  L  0  0  0  0 */
        lsl.l   #4,d0           /* YO XO YS XS S  M  R  L  0  0  0  0  0  0  0  0 */
11:
        btst    #4,(a0)
        beq.b   12f             /* handshake */
        dbra    d1,11b
        bra     timeout_err
12:
        move.b  (a0),d0         /* X7 X6 X5 X4 */
        move.b  #0x20,(a0)      /* next phase */
        lsl.b   #4,d0           /* YO XO YS XS S  M  R  L  X7 X6 X5 X4 0  0  0  0 */
        lsl.l   #4,d0           /* YO XO YS XS S  M  R  L  X7 X6 X5 X4 0  0  0  0  0  0  0  0 */
13:
        btst    #4,(a0)
        bne.b   14f             /* handshake */
        dbra    d1,13b
        bra     timeout_err
14:
        move.b  (a0),d0         /* X3 X2 X1 X0 */
        move.b  #0,(a0)         /* next phase */
        lsl.b   #4,d0           /* YO XO YS XS S  M  R  L  X7 X6 X5 X4 X3 X2 X1 X0 0  0  0  0 */
        lsl.l   #4,d0           /* YO XO YS XS S  M  R  L  X7 X6 X5 X4 X3 X2 X1 X0 0  0  0  0  0  0  0  0 */
15:
        btst    #4,(a0)
        beq.b   16f             /* handshake */
        dbra    d1,15b
        bra     timeout_err
16:
        move.b  (a0),d0         /* Y7 Y6 Y5 Y4 */
        move.b  #0x20,(a0)      /* next phase */
        lsl.b   #4,d0           /* YO XO YS XS S  M  R  L  X7 X6 X5 X4 X3 X2 X1 X0 Y7 Y6 Y5 Y4 0  0  0  0 */
        lsl.l   #4,d0           /* YO XO YS XS S  M  R  L  X7 X6 X5 X4 X3 X2 X1 X0 Y7 Y6 Y5 Y4 0  0  0  0  0  0  0  0*/
17:
        btst    #4,(a0)
        beq.b   18f             /* handshake */
        dbra    d1,17b
        bra     timeout_err
18:
        move.b  (a0),d0         /* Y3 Y2 Y1 Y0 */
        move.b  #0x60,(a0)      /* first phase */
        lsl.b   #4,d0           /* YO XO YS XS S  M  R  L  X7 X6 X5 X4 X3 X2 X1 X0 Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 0  0  0  0 */
        lsr.l   #4,d0           /* YO XO YS XS S  M  R  L  X7 X6 X5 X4 X3 X2 X1 X0 Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */
19:
        btst    #4,(a0)
        beq.b   19b             /* wait on handshake */

        move.w  d2,sr           /* restore int status */
        rts

timeout_err:
        move.b  #0x60,(a0)      /* first phase */
        nop
        nop
0:
        btst    #4,(a0)
        beq.b   0b              /* wait on handshake */

        move.w  d2,sr           /* restore int status */
        moveq   #-2,d0
        rts

mky_err:
        move.b  #0x40,6(a0)     /* set direction bits */
        nop
        nop
        move.b  #0x40,(a0)

        move.w  d2,sr           /* restore int status */
        moveq   #-1,d0
        rts


| Global variables for 68000

        .align  4

vblank:
        dc.l    0

        .global gen_lvl2
gen_lvl2:
        dc.w    0

        .global cd_ok
cd_ok:
        dc.w    0

        .align  4
