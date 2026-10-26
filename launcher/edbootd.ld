/* edbootd.ld - EDBOOTD's RAM program: everything in one block at 0xFF0000 */
SECTIONS
{
    . = 0xFF0000;
    .text : { *(.text.start) *(.text*) *(.rodata*) *(.data*) *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
