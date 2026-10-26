/*
 * edprobe.c - EDPROBE.BIN: what the console sees after ONE write of CTRL0's C bit, exactly as EDBOOT leaves it.
 *
 * EDBOOTD (the first diagnostic) wrote CTRL0 again before it looked at 0x400000, and its "BIOS at 0" check
 * only compared "SEGA" - which the EverDrive's own header has too.  This probe fixes both:
 *   - it writes C = 1 once and then never touches the EverDrive's registers unless asked;
 *   - it shows the header TEXT at 0x000100 and 0x400100 (the BIOS says "SEGA MEGA DRIVE", the EverDrive's
 *     bank 0 says "SEGA SSF"), and the reset PC at 0x000004 and 0x400004;
 *   - it looks for signatures it wrote into each of the eight 512 KB windows (in cartridge mode, before C),
 *     both at A22 = 0 (0x010000 + n * 0x80000) and at A22 = 1 (0x410000 + n * 0x80000), and does a write
 *     test at each.
 * Snapshot 1 is taken straight after the C write, snapshot 2 one second later.  Then, by hand:
 *   C      68000 RESET instruction (pulses the reset line the cartridge and the 32X see), then snapshot 3
 *   B      CTRL0 = 0xA800 written a second time, then snapshot 4
 *   START  boot the Sega CD BIOS as EDBOOT does (only if the BIOS is at 0x000000), no more writes
 * Runs from work RAM (edbootd.ld at 0xFF0000); the ROM part (edprobe_rom.s) copies it there.
 */
#include <stdint.h>
#include "../hello/src/font8x8.h"

#define VDP_DATA16  (*(volatile uint16_t *)0xC00000)
#define VDP_CTRL16  (*(volatile uint16_t *)0xC00004)
#define VDP_CTRL32  (*(volatile uint32_t *)0xC00004)
#define CTRL(n)     (*(volatile uint16_t *)(0xA130F0 + 2 * (n)))   /* EverDrive CTRL0-7 */
#define W16(a)      (*(volatile uint16_t *)(uint32_t)(a))
#define L32(a)      (*(volatile uint32_t *)(uint32_t)(a))
#define B8(a)       (*(volatile uint8_t *)(uint32_t)(a))

#define ED_RAM      0xA000      /* P + W */
#define ED_RAM_C    0xA800      /* P + W + C, as EDBOOT */

#define SIG(n)      ((uint16_t)(0xED00 | (n)))                   /* the word written into window n */
#define OFF(n)      ((uint32_t)(n) * 0x80000 + 0x10000)          /* past the 32 KB ROM in bank 0 */

enum { WHITE = 0, GREEN = 1, RED = 2, YELLOW = 3 };

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

#define PAD_B     0x10
#define PAD_C     0x20
#define PAD_START 0x80

static void wait_frames(int n)                  /* VDP vertical blank flag */
{
    while (n--)
    {
        while (!(VDP_CTRL16 & 8)) { }
        while (VDP_CTRL16 & 8) { }
    }
}

/* ---- what the console sees ---- */
typedef struct {
    int valid;
    char h0[17], h4[17];                        /* header text at 0x000100, 0x400100 */
    uint32_t pc0, pc4;                          /* reset PC at 0x000004, 0x400004 */
    uint8_t lo, hi;                             /* bit n: window n's signature at A22 = 0 / A22 = 1 */
    uint8_t wlo, whi;                           /* a word written at window 0 (+2) reads back */
} snap_t;

static snap_t snaps[4];
static const char *const names[4] = { "1  C=1 WRITTEN ONCE (AS EDBOOT)", "2  ONE SECOND LATER, NO WRITES",
                                      "3  AFTER 68000 RESET (C BUTTON)", "4  CTRL0 WRITTEN AGAIN (B)" };
static uint8_t cartsig;                         /* the signatures in cartridge mode, before C */
static const char *note = "";

static void header(char *d, uint32_t base)
{
    int i;
    for (i = 0; i < 8; i++)
    {
        uint16_t w = W16(base + 0x100 + 2 * i);
        d[2 * i] = (char)(w >> 8);
        d[2 * i + 1] = (char)w;
    }
    d[16] = 0;
}

static void snap(snap_t *s, uint16_t wv)        /* reads only, then the two write tests */
{
    int n;
    header(s->h0, 0x000000);
    s->pc0 = L32(0x000004);
    header(s->h4, 0x400000);
    s->pc4 = L32(0x400004);
    s->lo = s->hi = 0;
    for (n = 0; n < 8; n++)
    {
        if (W16(OFF(n)) == SIG(n)) s->lo |= (uint8_t)(1 << n);
        if (W16(0x400000 + OFF(n)) == SIG(n)) s->hi |= (uint8_t)(1 << n);
    }
    W16(OFF(0) + 2) = wv;                        /* the BIOS's ROM or the EverDrive's bank 0: harmless */
    s->wlo = W16(OFF(0) + 2) == wv;
    W16(0x400000 + OFF(0) + 2) = (uint16_t)~wv;
    s->whi = W16(0x400000 + OFF(0) + 2) == (uint16_t)~wv;
    s->valid = 1;
}

static int is_ssf(const char *h)  { return h[0] == 'S' && h[1] == 'E' && h[2] == 'G' && h[3] == 'A' && h[4] == ' ' && h[5] == 'S' && h[6] == 'S' && h[7] == 'F'; }
static int is_bios(const char *h) { return h[0] == 'S' && h[1] == 'E' && h[2] == 'G' && h[3] == 'A' && !is_ssf(h); }

static void bits(char *d, uint8_t m)            /* "01234567", '.' where not found */
{
    int n;
    for (n = 0; n < 8; n++) d[n] = (m & (1 << n)) ? (char)('0' + n) : '.';
}

static int block(int y, int i)
{
    const snap_t *s = &snaps[i];
    char t[64], *p;
    const char *k0, *k4;
    int good;

    put(1, y++, YELLOW, names[i]);
    p = str(t, "000100 "); p = str(p, s->h0); p = str(p, " PC "); p = hex(p, s->pc0, 8); *p = 0;
    put(1, y++, WHITE, t);
    p = str(t, "400100 "); p = str(p, s->h4); p = str(p, " PC "); p = hex(p, s->pc4, 8); *p = 0;
    put(1, y++, WHITE, t);
    p = str(t, "SIG LOW "); bits(p, s->lo); p += 8; p = str(p, " HIGH "); bits(p, s->hi); p += 8;
    p = str(p, s->wlo ? " WL+" : " WL-"); p = str(p, s->whi ? " WH+" : " WH-"); *p = 0;
    put(1, y++, WHITE, t);

    k0 = is_bios(s->h0) ? "BIOS" : is_ssf(s->h0) || s->lo == 0xFF ? "EVERDRIVE" : "NOTHING";
    if (s->hi == 0xFF)       k4 = s->whi ? "EVERDRIVE RAM" : "EVERDRIVE, NO WRITES";
    else if (is_bios(s->h4)) k4 = "SEGA CD BIOS";
    else if (is_ssf(s->h4))  k4 = "EVERDRIVE ROM";
    else                     k4 = "NOTHING";
    good = is_bios(s->h0) && s->hi == 0xFF && s->whi;
    p = str(t, "=> 0: "); p = str(p, k0); p = str(p, "  4: "); p = str(p, k4); *p = 0;
    put(1, y++, good ? GREEN : RED, t);
    return y;
}

static void redraw(void)
{
    char t[64], *p;
    int i, y = 1;
    vdp_init();
    put(1, y++, YELLOW, "EDPROBE - EVERDRIVE AFTER ONE C WRITE");
    p = str(t, "CART MODE A000: SIG "); bits(p, cartsig); p += 8; *p = 0;
    put(1, y++, cartsig == 0xFF ? GREEN : RED, t);
    for (i = 0; i < 4; i++)
        if (snaps[i].valid)
            y = block(y, i);
    y = 24;
    put(1, y++, WHITE, "C: 68000 RESET    B: CTRL0 AGAIN");
    put(1, y++, WHITE, "START: BOOT CD    (PHOTO FIRST)");
    if (*note) put(1, y, RED, note);
}

extern char handover[], handover_end[];       /* edprobe_crt.s */

static void boot(void)                          /* the BIOS's own reset start; the EverDrive untouched */
{
    VDP_CTRL16 = 0x8104;
    memcpy((void *)0xFFFF00, handover, (unsigned)(handover_end - handover));
    __asm__ volatile ("jmp 0xFFFF00" ::: "memory");
}

static void released(void) { while (pad()) { } }

void dmain(void)
{
    int n;
    char h[17];

    vdp_init();
    header(h, 0x400000);
    if (!is_bios(h))
    {
        put(1, 1, RED, "NO SEGA CD (NO BIOS AT 400000) - STOP");
        for (;;) { }
    }

    /* cartridge mode, as EDBOOT: windows 1-7 = banks 1-7, write on; a signature in each window */
    for (n = 1; n < 8; n++) CTRL(n) = (uint16_t)n;
    CTRL(0) = ED_RAM;
    for (n = 0; n < 8; n++) W16(OFF(n)) = SIG(n);
    cartsig = 0;
    for (n = 0; n < 8; n++)
        if (W16(OFF(n)) == SIG(n)) cartsig |= (uint8_t)(1 << n);

    /* the one write, then look - nothing else in between */
    CTRL(0) = ED_RAM_C;
    snap(&snaps[0], 0x6B01);
    wait_frames(60);
    snap(&snaps[1], 0x6B02);
    redraw();

    for (;;)
    {
        uint16_t k = pad();
        if (k & PAD_C)
        {
            released();
            __asm__ volatile ("reset" ::: "memory");
            if (B8(0xA10001) & 0x0F) L32(0xA14000) = 0x53454741;   /* TMSS, should the reset have locked it */
            for (volatile uint32_t i = 0; i < 100000; i++) { }     /* about half a second */
            snap(&snaps[2], 0x6B03);
            note = "";
            redraw();
        }
        else if (k & PAD_B)
        {
            released();
            CTRL(0) = ED_RAM_C;
            snap(&snaps[3], 0x6B04);
            note = "";
            redraw();
        }
        else if (k & PAD_START)
        {
            released();
            header(h, 0x000000);
            if (is_bios(h))
                boot();
            note = "NO BIOS AT 000000 - NOT BOOTING";
            redraw();
        }
    }
}
