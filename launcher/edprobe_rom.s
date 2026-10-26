| edprobe_rom.s - EDPROBE.BIN: the C-bit probe (edprobe.c).  This ROM part only copies the
| program (edprobe_ram.bin, linked at 0xFF0000) into work RAM and starts it there: after CTRL0's C
| bit the EverDrive's ROM is no longer at 0x000000.
        .text
        .org    0x000
        .long   0x00FFFE00                  | SSP
        .long   start                       | PC
        .rept   62
        .long   start
        .endr

        .org    0x100
        .ascii  "SEGA SSF        "          | the EverDrive's extended SSF mapper
        .ascii  "(C)MN99 2026.OCT"
        .ascii  "CD32X RAM CART: EVERDRIVE PRO PROBE             "
        .ascii  "CD32X RAM CART: EVERDRIVE PRO PROBE             "
        .ascii  "GM 00000000-00"
        .word   0x0000                      | checksum (edboot/build.sh)
        .ascii  "J               "
        .long   0x00000000, 0x00007FFF
        .long   0x00FF0000, 0x00FFFFFF
        .ascii  "            "
        .ascii  "            "
        .ascii  "                                        "
        .ascii  "JUE             "

        .org    0x200
start:
        move.w  #0x2700,sr
        lea     0x00FFFE00,sp
        move.b  0xA10001,d0                 | TMSS
        andi.b  #0x0F,d0
        beq.b   1f
        move.l  #0x53454741,0xA14000
1:      lea     program(pc),a0
        lea     0xFF0000,a1
        move.w  #(program_end - program) / 2 - 1,d0
2:      move.w  (a0)+,(a1)+
        dbra    d0,2b
        jmp     0xFF0000

        .align  2
program:
        .incbin "build/edprobe_ram.bin"
        .align  2
program_end:

        .org    0x7FFE
        .word   0xFFFF
