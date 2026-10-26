/* edsave2 - the EverDrive PRO's save bank (31) worked the way the cartridge-boot listener works it,
 * with and without an SH2 busy on the cart (CD32X BootCamp, 25 Oct 2026).
 *
 * OpenLara with its saves in bank 31 (EDSAVE-1) ran at the title but froze when the in-game passport
 * opened - the first time the 68000 READ the bank (through window 1, RV = 1) while the SH2s were
 * reading the level out of the cart.  The 68000 had only ever written the cart with RV = 1 in a game;
 * every read-back went through the 32X's own window (0x900000, RV = 0).  This ROM does each kind of
 * access as a 4 KB block, the slave SH2 idle or hammering window 4, and shows OK / BAD n / HANG:
 *   1-4  writes (words, bytes) and reads (bytes, words) of bank 31, slave idle
 *   5-8  the same reads and the word write with the slave reading window 4 nonstop
 *   9    bytes read from bank 0 through window 1, slave busy (the route the game load uses, read)
 *   10   bytes read from bank 31 through the 32X window 0x980000 with RV = 0, slave busy
 * HANG = the 68000 did not answer within 10 s: the phase shown is the one that locks.
 */
#include <stdint.h>
#include "32x.h"
#include "font8x8.h"

#ifndef ED_CTRL0
#define ED_CTRL0 0xA000
#endif
#define FIRST_BANK  1
#define LAST_BANK   27                      /* 28-31: the EverDrive's save area and OS - never touched */
#define WORDS       262144u                 /* one 512 KB window */

#define COMM8_32   (*(volatile uint32_t *)0x20004028)
#define COMM12_32  (*(volatile uint32_t *)0x2000402C)

/* ---- what crt0.s needs from the program ---- */
volatile uint32_t gTicks = 0;
volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;
void pri_vbi_handler(void) { gTicks++; }

void sec_dma1_handler(void) { }

/* the slave: reads window 4 (bank 4, pattern seed 0x4444, filled by the 68000) nonstop while asked */
/* shared between the SH2s: always through the cache-through mirror (each SH2 has its own cache) */
volatile uint32_t hammer_on_v = 0, slave_reads_v = 0, slave_bad_v = 0, slave_alive_v = 0;
#define CT(x) (*(volatile uint32_t *)((uint32_t)&(x) | 0x20000000u))
#define hammer_on   CT(hammer_on_v)
#define slave_reads CT(slave_reads_v)
#define slave_bad   CT(slave_bad_v)
#define slave_alive CT(slave_alive_v)
void secondary(void)
{
    const volatile uint16_t *w = (const volatile uint16_t *)(0x22000000u + (4u << 19));
    while (1)
    {
        slave_alive = slave_alive + 1;
        if (!hammer_on) continue;
        uint32_t reads = 0, bad = 0;
        for (uint32_t i = 0; i < 262144u && hammer_on; i += 16)
        {
            uint16_t v = w[i];
            uint16_t want = (uint16_t)((i & 0xFFFF) ^ (((i >> 16) & 3) << 12) ^ 0x4444);
            if (v != want) bad++;
            reads++;
        }
        slave_reads = slave_reads + reads;
        slave_bad = slave_bad + bad;
    }
}

/* ---- the 68000 side's commands (src-md/crt0.s) ---- */
static uint16_t last_cmd, last_arg;    /* the last 68000 command (for the stuck screen) */
static int md_cmd(uint16_t cmd, uint16_t arg)
{
    last_cmd = cmd; last_arg = arg;
    uint32_t t0 = gTicks;
    while (MARS_SYS_COMM0 != 0 && gTicks - t0 < 600) { }
    MARS_SYS_COMM2 = (short)arg;
    MARS_SYS_COMM0 = (short)cmd;
    t0 = gTicks;
    while (MARS_SYS_COMM0 != 0 && gTicks - t0 < 600) { }   /* a fill takes about 1 s; 10 s = no answer */
    return MARS_SYS_COMM0 == 0;
}

/* ---- the screen: 320 x 224, 256 colours, text 8 x 8 ---- */
#define FB          ((volatile uint16_t *)&MARS_FRAMEBUFFER)
enum { C_BLACK, C_WHITE, C_RED, C_GREEN, C_YELLOW, C_GREY, C_BLUE, C_CYAN };

static void palette(void)
{
    static const uint16_t pal[8] = { 0x0000, 0x7FFF, 0x001F, 0x03E0, 0x03FF, 0x4210, 0x7C00, 0x7FE0 };
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    while ((MARS_VDP_FBCTL & MARS_VDP_PEN) == 0) { }
    for (int i = 0; i < 256; i++)
        cram[i] = (i < 8) ? pal[i] : 0;
}

static void line_table(void)
{
    for (int i = 0; i < 256; i++)
        FB[i] = (uint16_t)(0x100 + (i < 224 ? i : 223) * 160);
}

static void clear(void)
{
    volatile uint16_t *p = FB + 0x100;
    for (int i = 0; i < 160 * 224; i++)
        p[i] = 0;
}

static void text(int x, int y, const char *s, uint8_t c)       /* x a multiple of 8 */
{
    for (; *s; s++, x += 8)
    {
        int ch = (uint8_t)*s;
        if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font8x8[ch - 32];
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 8; b += 2)
                FB[0x100 + (y + r) * 160 + ((x + b) >> 1)] =
                    (uint16_t)((((g[r] & (0x80 >> b)) ? c : 0) << 8) | ((g[r] & (0x40 >> b)) ? c : 0));
    }
}

static void flip(void)
{
    uint16_t cur = MARS_VDP_FBCTL & MARS_VDP_FS;
    MARS_VDP_FBCTL = (uint16_t)(cur ^ 1);
    while ((MARS_VDP_FBCTL & MARS_VDP_FS) == cur) { }
}

static char *hexn(char *d, uint32_t v, int n)
{
    for (int i = 0; i < n; i++)
        d[i] = "0123456789ABCDEF"[(v >> (4 * (n - 1 - i))) & 15];
    d[n] = 0;
    return d;
}

static char *dec(char *d, uint32_t v)
{
    char t[12]; int n = 0; char *s = d;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *d++ = t[--n];
    *d = 0;
    return s;
}


/* ---- the probe ---- */
#define BANK      31
#define WIN1      0x080000u                     /* window 1 */
#define OFF       0x00010000u                   /* the block: bank offset 0x10000 (OpenLara's slot 0) */

typedef struct { const char *name; int op; uint32_t addr; int bank; int hammer; int state; uint32_t bad, ms; } Phase;
/* op: 0 word write, 1 byte write, 2 byte read, 3 word read, 4 byte read RV=0; addr = 68000 address */
static Phase ph[10] = {
    { "1 WR WORDS B31 W1   SLAVE IDLE", 0, WIN1 + OFF, BANK, 0 },
    { "2 WR BYTES B31 W1   SLAVE IDLE", 1, WIN1 + OFF, BANK, 0 },
    { "3 RD BYTES B31 W1   SLAVE IDLE", 2, WIN1 + OFF, BANK, 0 },
    { "4 RD WORDS B31 W1   SLAVE IDLE", 3, WIN1 + OFF, BANK, 0 },
    { "5 RD BYTES B31 W1   SLAVE BUSY", 2, WIN1 + OFF, BANK, 1 },
    { "6 RD WORDS B31 W1   SLAVE BUSY", 3, WIN1 + OFF, BANK, 1 },
    { "7 WR WORDS B31 W1   SLAVE BUSY", 0, WIN1 + OFF, BANK, 1 },
    { "8 RD BYTES B31 W1   SLAVE BUSY", 2, WIN1 + OFF, BANK, 1 },
    { "9 RD BYTES B0  W1   SLAVE BUSY", 2, WIN1 + OFF, 0, 1 },
    { "10 RD BYTES B31 32X RV0  BUSY", 4, 0x900000u + WIN1 + OFF, BANK, 1 },
};
static const char *phase = "START";
static int cur = -1;

static void fail_stop(const char *why)
{
    text(8, 200, why, C_RED); flip();
    while (1) { }
}

static uint16_t pat(uint32_t addr, int i)       /* addr = the block's 68000 address */
{
    return (uint16_t)((((addr >> 1) + (uint32_t)i) * 0x9E37u) ^ 0xA5A5u);
}

static int verify_sh2(uint32_t winaddr)         /* the SH2 reads the block through window 1 */
{
    const volatile uint16_t *w = (const volatile uint16_t *)(0x22000000u + winaddr);
    int bad = 0;
    for (int i = 0; i < 2048; i++) if (w[i] != pat(winaddr, i)) bad++;
    return bad;
}

static void draw(void)
{
    char b[16];
    clear();
    text(8, 4, "EDSAVE2  THE SAVE BANK AS THE LISTENER", C_YELLOW);
    text(8, 16, "PHASE", C_GREY); text(56, 16, phase, C_WHITE);
    text(8, 28, "SLAVE RD", C_GREY); text(80, 28, dec(b, slave_reads), C_WHITE);
    text(160, 28, "BAD", C_GREY); text(192, 28, dec(b, slave_bad), slave_bad ? C_RED : C_WHITE);
    text(8, 38, "SLAVE ALIVE", C_GREY); text(104, 38, dec(b, slave_alive & 0xFFFF), C_WHITE);
    text(160, 38, "(BAD = READS HELD BY RV?)", C_GREY);
    for (int i = 0; i < 10; i++)
    {
        Phase *p = &ph[i];
        int y = 52 + i * 11;
        text(8, y, p->name, p->state == 2 ? C_RED : p->state == 1 ? C_GREEN : C_GREY);
        if (p->state == 1) { text(256, y, "OK", C_GREEN); text(280, y, dec(b, p->ms), C_GREY); }
        if (p->state == 2) { text(256, y, "BAD", C_RED); text(288, y, dec(b, p->bad), C_RED); }
        if (p->state == 3) { text(256, y, "HANG", C_RED); }
    }
    text(8, 172, "ALL OK: THE 68000 CAN READ THE BANK", C_GREY);
    text(8, 182, "WITH THE SH2 ON THE CART.", C_GREY);
    text(8, 192, "HANG/BAD AT 5-8: THAT IS THE FREEZE.", C_GREY);
    text(8, 202, "MS = TIME FOR 4 KB (1 = 16.7 MS)", C_GREY);
    flip();
}

static int block(int op, uint32_t addr, uint32_t *bad)   /* 1 ok, 0 no answer */
{
    MARS_SYS_COMM8 = (short)(addr >> 16);
    MARS_SYS_COMM10 = (short)addr;
    if (!md_cmd((uint16_t)(0x1400 | op), 0)) return 0;
    *bad = ((uint32_t)(uint16_t)MARS_SYS_COMM8 << 16) | (uint16_t)MARS_SYS_COMM10;
    return 1;
}

int main(void)
{
    MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256;
    palette();
    for (int i = 0; i < 2; i++) { line_table(); clear(); flip(); }
    draw();

    if (!md_cmd(0x0600, 0x8000)) fail_stop("68000 NOT ANSWERING");      /* W off, banks 0-7 */
    phase = "FILL B4"; draw();
    if (!md_cmd(0x0704, 0x4444)) fail_stop("68000 NOT ANSWERING");      /* window 4 = bank 4: the slave's food */
    /* bank 0 through window 1 holds this ROM (128 KB) and then whatever: give the block there a pattern too */
    if (!md_cmd(0x0601, 0)) fail_stop("68000 NOT ANSWERING");
    { uint32_t b; if (!block(0, WIN1 + OFF, &b)) fail_stop("68000 NOT ANSWERING (B0 FILL)"); }
    if (!md_cmd(0x0601, 1)) fail_stop("68000 NOT ANSWERING");

    for (cur = 0; cur < 10; cur++)
    {
        Phase *p = &ph[cur];
        phase = p->name; draw();
        hammer_on = p->hammer;
        if (p->hammer) { uint32_t t0 = gTicks; while (gTicks - t0 < 30) { } }   /* let the slave get going */
        if (!md_cmd(0x0601, (uint16_t)p->bank)) fail_stop("68000 NOT ANSWERING (CTRL1)");
        uint32_t t0 = gTicks, bad = 0;
        int ok = block(p->op, p->addr, &bad);
        p->ms = gTicks - t0;
        if (!ok) { p->state = 3; draw(); hammer_on = 0; break; }
        if (p->op <= 1)                                   /* a write: the SH2 checks it */
        {
            hammer_on = 0;
            if (!md_cmd(0x0601, (uint16_t)p->bank)) fail_stop("68000 NOT ANSWERING");
            bad = (uint32_t)verify_sh2(WIN1 + OFF);
        }
        p->bad = bad;
        p->state = bad ? 2 : 1;
        hammer_on = 0;
        if (!md_cmd(0x0601, 1)) fail_stop("68000 NOT ANSWERING");
        draw();
    }
    phase = "DONE"; draw();
    while (1) { }
}
