| edboot.s - EDBOOT.BIN: the Mega EverDrive PRO as the RAM cart of a CD32X disc.
|
| Start it from the EverDrive menu with the CD32X disc in the Sega CD (Genesis + Sega CD + 32X, the
| EverDrive in the 32X slot). It:
|   1. checks that a Sega CD is there (its BIOS at 0x400000 while a cartridge runs);
|   2. sets the EverDrive's extended SSF mapper as a 4 MB RAM cart: banks 0-7 in windows 0-7, write on
|      (CTRL0 = 0xA000, the value console-tested with edtest);
|   3. from work RAM, sets CTRL0's C bit (0xA800): CART_IN is released, the console now sees no
|      cartridge, so the Sega CD BIOS is at 0x000000 and the EverDrive's memory at 0x400000 - where a CD32X
|      stack expects its RAM cart;
|   4. checks that the BIOS really is at 0x000000, then starts it as a reset would: the BIOS boots the disc.
| The screen colour tells what happened if it stops: BLUE = no Sega CD, RED = the C bit did not release
| the cartridge (CART_IN back on, the EverDrive unchanged). Nothing else is drawn.
|
| Not yet run on a console: whether C = 1 releases CART_IN and keeps the mapper answering at 0x400000 is
| the open question this ROM answers (the EverDrive's own "Run Mega-CD" releases CART_IN, with its
| backup-RAM cart instead of this RAM). docs/10_EVERDRIVE_TEST.md.

        .equ    CTRL0,      0xA130F0
        .equ    CTRL1,      0xA130F2
        .equ    ED_RAM,     0xA000          | P + W, bank 0 in window 0
        .equ    ED_RAM_CD,  0xA800          | P + W + C: CART_IN released
        .equ    HANDOVER,   0xFF0000        | work RAM

        .text
        .org    0x000
        .long   0x00FFFE00                  | SSP
        .long   start                       | PC
        .rept   62
        .long   start                       | every other vector: start again
        .endr

        .org    0x100
        .ascii  "SEGA SSF        "          | the EverDrive's extended SSF mapper
        .ascii  "(C)MN99 2026.OCT"
        .ascii  "CD32X RAM CART: EVERDRIVE PRO LAUNCHER          "
        .ascii  "CD32X RAM CART: EVERDRIVE PRO LAUNCHER          "
        .ascii  "GM 00000000-00"
        .word   0x0000                      | checksum (tools: edboot/build.sh)
        .ascii  "J               "          | device support
        .long   0x00000000, 0x00007FFF      | ROM
        .long   0x00FF0000, 0x00FFFFFF      | RAM
        .ascii  "            "              | no SRAM
        .ascii  "            "
        .ascii  "                                        "
        .ascii  "JUE             "

        .org    0x200
start:
        move.w  #0x2700,sr
        lea     0x00FFFE00,sp

        | TMSS: a console with a version above 0 needs "SEGA" at 0xA14000 before the VDP is used
        move.b  0xA10001,d0
        andi.b  #0x0F,d0
        beq.b   1f
        move.l  #0x53454741,0xA14000
1:
        | 1. a Sega CD? While a cartridge runs, its BIOS is at 0x400000 ("SEGA" at 0x400100)
        cmpi.l  #0x53454741,0x400100
        bne     no_cd

        | 2. the 4 MB RAM cart: windows 1-7 = banks 1-7, then write on (window 0 = bank 0)
        lea     CTRL1,a0
        moveq   #1,d0
2:      move.w  d0,(a0)+
        addq.w  #1,d0
        cmpi.w  #8,d0
        bne.b   2b
        move.w  #ED_RAM,CTRL0

        | 3. releasing CART_IN takes this ROM away from 0x000000: the rest runs from work RAM
        lea     handover(pc),a0
        lea     HANDOVER,a1
        move.w  #(handover_end - handover) / 2 - 1,d0
3:      move.w  (a0)+,(a1)+
        dbra    d0,3b
        jmp     HANDOVER

| ---- runs at 0xFF0000 (position independent) ------------------------------------------------------
handover:
        move.w  #ED_RAM_CD,CTRL0            | C = 1: CART_IN released
        | 4. the BIOS at 0x000000 now? Its header says "SEGA MEGA DRIVE"; this ROM's says "SEGA SSF"
        cmpi.l  #0x53454741,0x000100        | "SEGA"
        bne.b   4f
        cmpi.l  #0x20535346,0x000104        | " SSF": still this ROM - nothing changed
        beq.b   4f
        movea.l 0x000000,sp                 | as a reset: the BIOS's stack and entry
        movea.l 0x000004,a0
        jmp     (a0)
4:      move.w  #ED_RAM,CTRL0               | CART_IN back on
        move.w  #0x000E,d1                  | red
        bra.b   colour
no_cd_h:
        move.w  #0x0E00,d1                  | blue
colour:
        lea     0xC00004,a1
        tst.w   (a1)                        | a half-written VDP command word is dropped
        move.w  #0x8004,(a1)                | mode 5, no H-int
        move.w  #0x8104,(a1)                | display off: the whole screen is the backdrop colour
        move.w  #0x8700,(a1)                | background = colour 0
        move.l  #0xC0000000,(a1)            | CRAM 0
        move.w  d1,0xC00000
5:      bra.b   5b
handover_end:

no_cd:
        bra.b   no_cd_h                     | (in ROM: the cartridge is still on)

        .org    0x7FFE
        .word   0xFFFF
