| edcd_sp.s - EDCD.32X's Sub-CPU start-up program, at PRG-RAM 0x6000 (docs/12_CARTRIDGE_BOOT.md).
| What the disc boot block's program (US_BOOT.BIN, 0x1000) does for the stack, minus loading it: the
| stack's program is already at 0x8000 (EDCD put it there).  The Sub-CPU BIOS calls init, then main.
        .text
sp_header:
        .asciz  "MAIN-SUBCPU"
        .word   0x0001,0x0000
        .long   0x00000000
        .long   0x00000000
        .long   sp_offsets-sp_header
        .long   0x00000000
sp_offsets:
        .word   sp_init-sp_offsets
        .word   sp_main-sp_offsets
        .word   sp_int2-sp_offsets
        .word   sp_user-sp_offsets
        .word   0x0000

| init: 2M Word RAM, priority mode off; tell the main CPU "I" (as the boot block's program)
sp_init:
        andi.b  #0xE2,0x8003.w
        move.b  #'I,0x800F.w
        rts

| main: wait for the main CPU's "B" (it gave us Word RAM), then the stack's program; it never returns
sp_main:
1:      cmpi.b  #'B,0x800E.w
        bne.b   1b
        move.b  #0,0x800F.w
        jsr     0x8000.l
2:      bra.b   2b

sp_int2:
sp_user:
        rts
