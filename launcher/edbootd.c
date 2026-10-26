/*
 * edbootd.c - EDBOOTD.BIN, the diagnostic EDBOOT: what does a Mega EverDrive PRO do once CTRL0's C bit has
 * released the cartridge line (CART_IN)?
 *
 * EDBOOT.BIN on the console: the Sega CD BIOS booted the CD32X disc and the 32X started (the C bit works),
 * but copying the game into the cart failed.  After C, the console reaches the cart at 0x400000 (Sega CD
 * boot), so the EverDrive sees A22 = 1 - never the case in cartridge mode, where edtest proved its RAM.
 * This program measures exactly that, with the 68000 alone (the 32X stays off), and prints the numbers.
 *
 * Runs entirely from work RAM (0xFF0000): after C = 1 the EverDrive's ROM is gone from 0x000000.
 * The ROM part (edbootd_rom.s) copies it here.  START (or A) then boots the Sega CD as EDBOOT does.
 */
#include <stdint.h>
#include "../hello/src/font8x8.h"

#define VDP_DATA16  (*(volatile uint16_t *)0xC00000)
#define VDP_CTRL16  (*(volatile uint16_t *)0xC00004)
#define VDP_CTRL32  (*(volatile uint32_t *)0xC00004)
#define CTRL(n)     (*(volatile uint16_t *)(0xA130F0 + 2 * (n)))   /* EverDrive CTRL0-7 */
#define W16(a)      (*(volatile uint16_t *)(uint32_t)(a))
#define B8(a)       (*(volatile uint8_t *)(uint32_t)(a))

#define ED_RAM      0xA000      /* P + W: as edtest and EDBOOT */
#define ED_RAM_C    0xA800      /* P + W + C: CART_IN released */
#define ED_RAM_XC   0xE800      /* P + X + W + C */

enum { WHITE = 0, GREEN = 1, RED = 2, YELLOW = 3 };   /* palette lines: colour 1 of each */

void *memset(void *d, int c, unsigned n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }

/* ---- the screen: plane A at 0xC000, 64 x 32 cells, font tiles 32-127 at VRAM 0x0400 ---- */
static void vdp_init(void)
{
    static const uint16_t regs[] = { 0x8004, 0x8104, 0x8230, 0x832C, 0x8407, 0x855E, 0x8600, 0x8700, 0x8800,
                                     0x8900, 0x8A00, 0x8B00, 0x8C81, 0x8D2E, 0x8E00, 0x8F02, 0x9001, 0x9100,
                                     0x9200 };
    static const uint16_t text[4] = { 0x0EEE, 0x00E0, 0x000E, 0x00EE };   /* white, green, red, yellow */
    unsigned i, p, c, r, b;

    (void)VDP_CTRL16;                               /* a half-written command word is dropped */
    for (i = 0; i < sizeof regs / sizeof regs[0]; i++)
        VDP_CTRL16 = regs[i];
    VDP_CTRL32 = 0x40000000;                        /* VRAM 0: clear all 64 KB */
    for (i = 0; i < 0x8000; i++)
        VDP_DATA16 = 0;
    VDP_CTRL32 = 0xC0000000;                        /* CRAM 0 */
    for (p = 0; p < 4; p++)
        for (c = 0; c < 16; c++)
            VDP_DATA16 = (c == 1) ? text[p] : 0;
    VDP_CTRL32 = 0x40000010;                        /* VSRAM 0 */
    VDP_DATA16 = 0;
    VDP_DATA16 = 0;
    VDP_CTRL32 = 0x44000000;                        /* VRAM 0x0400: tiles 32-127 */
    for (c = 0; c < 96; c++)
        for (r = 0; r < 8; r++)
        {
            uint8_t bits = font8x8[c][r];
            uint16_t hi = 0, lo = 0;
            for (b = 0; b < 4; b++)
            {
                if (bits & (0x80 >> b)) hi |= (uint16_t)(1 << (12 - 4 * b));
                if (bits & (0x08 >> b)) lo |= (uint16_t)(1 << (12 - 4 * b));
            }
            VDP_DATA16 = hi;
            VDP_DATA16 = lo;
        }
    VDP_CTRL16 = 0x8144;                            /* display on */
}

static void put(int x, int y, int pal, const char *s)
{
    uint32_t addr = 0xC000 + (uint32_t)(y * 64 + x) * 2;
    VDP_CTRL32 = 0x40000000 | ((addr & 0x3FFF) << 16) | (addr >> 14);
    for (; *s; s++)
    {
        unsigned ch = (uint8_t)*s;
        if (ch < 32 || ch > 127) ch = '?';
        VDP_DATA16 = (uint16_t)((pal << 13) | ch);
    }
}

static char *hex(char *d, uint32_t v, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--) { d[i] = "0123456789ABCDEF"[v & 15]; v >>= 4; }
    return d + n;
}

static char *str(char *d, const char *s) { while (*s) *d++ = *s++; return d; }

/* "LABEL aaaa bbbb ..." : n words from addr */
static void words(int y, const char *label, uint32_t addr, int n, const uint16_t *want)
{
    char t[64], *p = t;
    int i, ok = 1;
    p = str(p, label);
    for (i = 0; i < n; i++)
    {
        uint16_t v = W16(addr + 2 * i);
        *p++ = ' ';
        p = hex(p, v, 4);
        if (want && v != want[i]) ok = 0;
    }
    if (want) p = str(p, ok ? "  OK" : "  NO");
    *p = 0;
    put(1, y, want ? (ok ? GREEN : RED) : WHITE, t);
}

/* write two words and a byte at addr, read them back: 1 = all there */
static int write_test(int y, const char *label, uint32_t addr, uint16_t a, uint16_t b, uint8_t c)
{
    char t[64], *p = t;
    uint16_t ra, rb;
    uint8_t rc;
    W16(addr) = a;
    W16(addr + 2) = b;
    B8(addr + 5) = c;                               /* odd byte, as safe_cart_xfer writes */
    ra = W16(addr);
    rb = W16(addr + 2);
    rc = B8(addr + 5);
    int ok = ra == a && rb == b && rc == c;
    p = str(p, label);
    p = str(p, " W ");
    p = hex(p, a, 4); *p++ = ' '; p = hex(p, b, 4); *p++ = ' '; p = hex(p, c, 2);
    p = str(p, " R ");
    p = hex(p, ra, 4); *p++ = ' '; p = hex(p, rb, 4); *p++ = ' '; p = hex(p, rc, 2);
    p = str(p, ok ? " OK" : " NO");
    *p = 0;
    put(1, y, ok ? GREEN : RED, t);
    return ok;
}

static int bios_at(uint32_t base)              /* "SEGA" at base + 0x100 */
{
    return W16(base + 0x100) == 0x5345 && W16(base + 0x102) == 0x4741;
}

static uint16_t pad(void)                       /* 3-button pad 1, active high: U D L R B C A START */
{
    volatile uint8_t *data = (volatile uint8_t *)0xA10003, *ctl = (volatile uint8_t *)0xA10009;
    uint8_t a, b;
    *ctl = 0x40;
    *data = 0x40;
    __asm__ volatile ("nop\n\tnop\n\tnop\n\tnop");
    a = *data;
    *data = 0x00;
    __asm__ volatile ("nop\n\tnop\n\tnop\n\tnop");
    b = *data;
    *data = 0x40;
    return (uint16_t)(~((a & 0x3F) | ((b & 0x30) << 2)) & 0xFF);
}

#define PAD_A     0x40
#define PAD_START 0x80

/* the Sega CD boot test, with CTRL0 = ctrl0: 2 = reads and writes work, 1 = reads only, 0 = no answer */
static int mode2(int y, uint16_t ctrl0, const char *name, uint16_t wa, uint16_t wb, uint8_t wc)
{
    static const uint16_t vectors[4] = { 0x00FF, 0xFE00, 0x0000, 0x0200 };   /* this ROM, bank 0 */
    static const uint16_t mode1[2]   = { 0x5AA5, 0xA55A };                    /* written at cart 1 MB */
    char t[64], *p;
    int reads, writes, banks;
    uint16_t a, b;

    CTRL(0) = ctrl0;
    p = str(t, "CTRL0 = "); p = hex(p, ctrl0, 4); p = str(p, " ("); p = str(p, name); p = str(p, ")");
    *p = 0;
    put(1, y++, YELLOW, t);
    words(y++, "400000 =", 0x400000, 4, vectors);
    words(y++, "500000 =", 0x500000, 2, mode1);
    reads = W16(0x400000) == vectors[0] && W16(0x400002) == vectors[1] && W16(0x500000) == mode1[0];
    writes = write_test(y++, "500010", 0x500010, wa, wb, wc);
    CTRL(2) = 9;                                    /* window 2 = bank 9: 0x500000 must change */
    a = W16(0x500010);
    CTRL(2) = 2;                                    /* and back */
    b = W16(0x500010);
    banks = a != b && b == wa;
    p = str(t, "BANK 9: 500010="); p = hex(p, a, 4); p = str(p, "  BANK 2: "); p = hex(p, b, 4);
    p = str(p, banks ? " OK" : " NO");
    *p = 0;
    put(1, y++, banks ? GREEN : RED, t);
    p = str(t, "400001=");  p = hex(p, B8(0x400001), 2);
    p = str(p, "  600001="); p = hex(p, B8(0x600001), 2);
    p = str(p, "  7FFFFF="); p = hex(p, B8(0x7FFFFF), 2);
    *p = 0;
    put(1, y++, WHITE, t);
    return reads && writes && banks ? 2 : reads ? 1 : 0;
}

extern char handover[], handover_end[];       /* edbootd_crt.s */

static void boot(uint16_t ctrl0)                /* as EDBOOT: the Sega CD BIOS, as a reset would start it */
{
    VDP_CTRL16 = 0x8104;
    memcpy((void *)0xFFFF00, handover, (unsigned)(handover_end - handover));
    __asm__ volatile ("move.w %0,%%d0\n\tjmp 0xFFFF00" :: "d"(ctrl0) : "d0", "memory");
}

void dmain(void)
{
    static const uint16_t vectors[4] = { 0x00FF, 0xFE00, 0x0000, 0x0200 };
    char t[64], *p;
    int y = 1, n, r0, r1, released;

    vdp_init();
    put(1, y++, YELLOW, "EDBOOTD - EVERDRIVE PRO AFTER THE C BIT");
    put(1, y++, WHITE, "CD32X BOOTCAMP DOCS/10");
    y++;
    if (!bios_at(0x400000))
    {
        put(1, y, RED, "NO SEGA CD (NO BIOS AT 400000) - STOP");
        for (;;) { }
    }
    put(1, y++, GREEN, "SEGA CD BIOS AT 400000");

    /* 1. cartridge mode, as edtest: the 4 MB cart, write on */
    for (n = 1; n < 8; n++) CTRL(n) = (uint16_t)n;
    CTRL(0) = ED_RAM;
    put(1, y++, YELLOW, "CARTRIDGE MODE, CTRL0 = A000");
    words(y++, "000000 =", 0x000000, 4, vectors);
    write_test(y++, "100000", 0x100000, 0x5AA5, 0xA55A, 0x3C);

    /* 2. C = 1: is the BIOS at 0 now (the cart let go)? */
    CTRL(0) = ED_RAM_C;
    released = bios_at(0x000000);
    p = str(t, "C=1: 000100="); p = hex(p, W16(0x100), 4); *p++ = ' '; p = hex(p, W16(0x102), 4);
    p = str(p, released ? " BIOS: CART OFF" : " NO CHANGE");
    *p = 0;
    put(1, y++, released ? GREEN : RED, t);
    if (!released)
    {
        CTRL(0) = ED_RAM;
        put(1, y + 1, RED, "C DID NOT RELEASE THE CART - STOP");
        for (;;) { }
    }

    /* 3. the cart at 0x400000 (A22 = 1), without and with the X bit */
    y++;
    r0 = mode2(y, ED_RAM_C, "X = 0", 0x1234, 0xEDCB, 0x5A);
    y += 7;
    r1 = mode2(y, ED_RAM_XC, "X = 1", 0x4321, 0xBCDE, 0xA5);
    y += 7;

    static const char *const verdict[3] = { "NO MEMORY AT 400000", "READS ONLY AT 400000", "RAM AT 400000 WORKS" };
    p = str(t, "X=0: "); p = str(p, verdict[r0]);
    *p = 0;
    put(1, y++, r0 == 2 ? GREEN : RED, t);
    p = str(t, "X=1: "); p = str(p, verdict[r1]);
    *p = 0;
    put(1, y++, r1 == 2 ? GREEN : RED, t);
    put(1, y + 1, WHITE, "START: BOOT CD (X=0)   A: BOOT CD (X=1)");

    for (;;)
    {
        uint16_t k = pad();
        if (k & (PAD_START | PAD_A))
        {
            while (pad()) { }                       /* released first: a held START sends the BIOS to its menu */
            boot((k & PAD_START) ? ED_RAM_C : ED_RAM_XC);
        }
    }
}
