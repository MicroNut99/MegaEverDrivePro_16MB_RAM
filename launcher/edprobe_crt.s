| edprobe_crt.s - EDPROBE's RAM program entry, linked at 0xFF0000 (edbootd.ld)
        .section .text.start
        .global _start
_start:
        move.w  #0x2700,sr
        lea     0x00FFFE00,sp
        jsr     dmain
1:      bra.b   1b

| handover: copied to 0xFFFF00 and run there.  Leaves the EverDrive's registers alone, clears work RAM
| 0xFF0000-0xFFFEFF (the BIOS must find RAM as after power-on, or it stops at its menu), then starts the
| Sega CD BIOS as a reset would.
        .text
        .global handover, handover_end
handover:
        lea     0xFF0000,a0
        move.w  #(0xFFFF00 - 0xFF0000) / 4 - 1,d1
1:      clr.l   (a0)+
        dbra    d1,1b
        movea.l 0x000000,sp
        movea.l 0x000004,a0
        jmp     (a0)
handover_end:
