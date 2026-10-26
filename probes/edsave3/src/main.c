/* edsave3 - is an SH2 held off the cart while the 68000 works it with RV = 1, or does it read garbage?
 * (CD32X BootCamp, 26 Oct 2026)
 *
 * OpenLara with its saves in the EverDrive's bank 31 (EDSAVE-1/2) runs at the title but the master
 * SH2 dies right after a save command completes when the passport opens IN the game - with a PSRAM
 * bank instead of 31 and with window 7 instead of window 1 just the same (EDSAVE-3 launchers).  The
 * boot stack and the engine both assume what the 32X manual implies: with RV = 1 an SH2 access to
 * the cart is HELD until RV = 0 (the level loads rely on it).  If on the EverDrive it is NOT held
 * but answered with whatever is on the bus, an SH2 that fetches code or data during a copy gets
 * garbage, and dies a little later in its own code - exactly the picture.  edsave2 could not tell:
 * its fill ran with W off, so the slave compared against unwritten memory (BAD = 100% either way).
 *
 * Here the fills are made with W on, and each phase runs 64 x 4 KB blocks of bank 31 the way the
 * listener does them (RV = 1; W on inside the writes) while:
 *   the SLAVE reads window 4 nonstop and compares     (S: bad words it saw during the phase)
 *   the MASTER reads window 2 between its polls       (M: the same, from the master)
 * Phases:
 *   1 FILL CHECK           68000 idle 1 s: S must be 0 (the fill works) - else nothing below counts
 *   2 RD W1 B31  RV1       64 reads of bank 31 through window 1, slave busy
 *   3 WR W1 B31  RV1 W     64 writes (W on inside), slave busy
 *   4 RD W7 B31  RV1       the same reads through window 7
 *   5 RD W1 B1   RV1       reads of window 1 with its own bank (no bank switch), slave busy
 *   6 RD 32X RV0           reads through the 32X's own window (RV = 0), slave busy: control
 *   7 RD W1 B31  RV1       slave idle, the MASTER reads window 2 between polls
 *   8 RD W1 B31  RV1       both busy
 *   9 RD W1 B31  RV1 CODE  the slave EXECUTES code out of window 4 (a copy of a function) nonstop
 * Columns: S = slave bad words, SR = slave reads (thousands), M = master bad words, T = ticks.
 * HANG = the 68000 did not answer within 10 s.
 *   S = 0 everywhere: the SH2s are held (or read right) - the freeze is not this.
 *   S > 0 at 2-5 and 0 at 1 and 6: an SH2 read during RV = 1 returns garbage on the EverDrive -
 *   the game must keep both SH2s off the cart (SDRAM code, interrupts masked) for every copy.
 */
#include <stdint.h>
#include "32x.h"
#include "font8x8.h"

#define COMM8_32   (*(volatile uint32_t *)0x20004028)

/* ---- what crt0.s needs from the program ---- */
volatile uint32_t gTicks = 0;
volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;
void pri_vbi_handler(void) { gTicks++; }
void sec_dma1_handler(void) { }

/* shared between the SH2s: always through the cache-through mirror (each SH2 has its own cache) */
volatile uint32_t hammer_on_v = 0, slave_reads_v = 0, slave_bad_v = 0, slave_alive_v = 0, slave_mode_v = 0;
#define CT(x) (*(volatile uint32_t *)((uint32_t)&(x) | 0x20000000u))
#define hammer_on   CT(hammer_on_v)
#define slave_reads CT(slave_reads_v)
#define slave_bad   CT(slave_bad_v)
#define slave_alive CT(slave_alive_v)
#define slave_mode  CT(slave_mode_v)

static uint16_t want4(uint32_t i) { return (uint16_t)((i & 0xFFFF) ^ (((i >> 16) & 3) << 12) ^ 0x4444); }
static uint16_t want2(uint32_t i) { return (uint16_t)((i & 0xFFFF) ^ (((i >> 16) & 3) << 12) ^ 0x2222); }

/* phase 9: a function the 68000 copies into window 4 (bank 4, offset 0x70000) and the slave runs
   from there - instruction fetches out of the cart while the 68000 holds RV.  It returns a constant;
   anything else = a fetch went wrong (or the slave is gone). */
#define CODE_WIN   (0x02000000u + (4u << 19) + 0x70000u)      /* cached, as code runs */
__attribute__((noinline, section(".text.probe"))) uint32_t probe_fn(uint32_t x)
{
    uint32_t a = x * 0x9E37u + 0x1234u;
    a ^= (a >> 7); a += 0x55AAu; a ^= (a << 3);
    return a;
}

void secondary(void)
{
    const volatile uint16_t *w4 = (const volatile uint16_t *)(0x22000000u + (4u << 19));
    while (1)
    {
        slave_alive = slave_alive + 1;
        if (!hammer_on) continue;
        uint32_t reads = 0, bad = 0;
        if (slave_mode == 1)
        {   /* code out of window 4 */
            uint32_t (*f)(uint32_t) = (uint32_t (*)(uint32_t))CODE_WIN;
            uint32_t want = probe_fn(7);
            for (uint32_t i = 0; i < 4096u && hammer_on; i++)
            {
                *(volatile uint8_t *)0xFFFFFE92u = 0x11;     /* CCR: purge the cache - fetch it again */
                if (f(7) != want) bad++;
                reads++;
            }
        }
        else
        {
            for (uint32_t i = 0; i < 262144u && hammer_on; i += 16)
            {
                if (i >= 0x38000u && i < 0x38080u) continue;   /* probe_fn's copy lives there */
                uint16_t v = w4[i];
                if (v != want4(i)) bad++;
                reads++;
            }
        }
        slave_reads = slave_reads + reads;
        slave_bad = slave_bad + bad;
    }
}

/* ---- the 68000 side's commands (src-md/crt0.s) ---- */
static uint32_t master_bad = 0, master_reads = 0;
static int master_hammer = 0;
static void master_chunk(void)                   /* 256 words of window 2, cache-through */
{
    static uint32_t pos = 0;
    const volatile uint16_t *w2 = (const volatile uint16_t *)(0x22000000u + (2u << 19));
    for (int n = 0; n < 256; n++)
    {
        uint32_t i = pos;
        if (w2[i] != want2(i)) master_bad++;
        master_reads++;
        pos = (pos + 16) & 0x3FFFFu;
    }
}
static int md_cmd(uint16_t cmd, uint16_t arg)
{
    uint32_t t0 = gTicks;
    while (MARS_SYS_COMM0 != 0 && gTicks - t0 < 600) { }
    MARS_SYS_COMM2 = (short)arg;
    MARS_SYS_COMM0 = (short)cmd;
    t0 = gTicks;
    while (MARS_SYS_COMM0 != 0 && gTicks - t0 < 600)
    {
        if (master_hammer) master_chunk();
    }
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
#define WIN1      0x080000u
#define WIN7      0x380000u
#define OFF       0x00010000u                   /* the block: bank offset 0x10000 (OpenLara's slot 0) */
#define REPEAT    64

typedef struct { const char *name; int op; uint32_t addr; int win; int bank; int slave; int master; int code;
                 int state; uint32_t s_bad, s_reads, m_bad, ticks; } Phase;
/* op: -1 nothing (1 s), 0 word write, 2 byte read, 4 byte read RV=0; win/bank: the window register set to
   the bank for the phase (and back to its own bank after) */
static Phase ph[9] = {
    { "1 FILL CHK 68K IDLE  ", -1, 0,           0, 0,    1, 0, 0 },
    { "2 RD W1 B31 RV1 SLV ",  2, WIN1 + OFF,  1, BANK, 1, 0, 0 },
    { "3 WR W1 B31 RV1 SLV ",  0, WIN1 + OFF,  1, BANK, 1, 0, 0 },
    { "4 RD W7 B31 RV1 SLV ",  2, WIN7 + OFF,  7, BANK, 1, 0, 0 },
    { "5 RD W1 B1  RV1 SLV ",  2, WIN1 + OFF,  1, 1,    1, 0, 0 },
    { "6 RD 32X    RV0 SLV ",  4, 0x900000u + WIN1 + OFF, 1, BANK, 1, 0, 0 },
    { "7 RD W1 B31 RV1 MST ",  2, WIN1 + OFF,  1, BANK, 0, 1, 0 },
    { "8 RD W1 B31 RV1 BOTH",  2, WIN1 + OFF,  1, BANK, 1, 1, 0 },
    { "9 RD W1 B31 RV1 CODE",  2, WIN1 + OFF,  1, BANK, 1, 0, 1 },
};
static const char *phase = "START";

static void fail_stop(const char *why)
{
    clear(); text(8, 100, why, C_RED); flip();
    while (1) { }
}

static void draw(void)
{
    char b[16];
    clear();
    text(8, 4, "EDSAVE3  SH2 HELD BY RV, OR GARBAGE?", C_YELLOW);
    text(8, 16, "PHASE", C_GREY); text(56, 16, phase, C_WHITE);
    text(8, 28, "SLAVE ALIVE", C_GREY); text(104, 28, dec(b, slave_alive & 0xFFFF), C_WHITE);
    text(184, 40, "S    SR   M    T", C_GREY);
    for (int i = 0; i < 9; i++)
    {
        Phase *p = &ph[i];
        int y = 52 + i * 11;
        text(8, y, p->name, p->state == 2 ? C_RED : p->state == 1 ? C_GREEN : C_GREY);
        if (p->state == 1 || p->state == 2)
        {
            text(184, y, dec(b, p->s_bad), p->s_bad ? C_RED : C_WHITE);
            text(232, y, dec(b, p->s_reads >= 1000 ? p->s_reads / 1000 : p->s_reads), C_GREY);   /* K, or the count itself below 1000 */
            text(272, y, dec(b, p->m_bad), p->m_bad ? C_RED : C_WHITE);
            text(304, y, dec(b, p->ticks), C_GREY);
        }
        if (p->state == 3) text(184, y, "HANG", C_RED);
    }
    text(8, 160, "S/M = BAD WORDS THE SH2 READ (PHASE)", C_GREY);
    text(8, 170, "SR = SLAVE READS (K)  T = TICKS/16.7MS", C_GREY);
    text(8, 182, "1 MUST BE 0. S>0 AT 2-5 AND 0 AT 6 =", C_GREY);
    text(8, 192, "SH2 READS DURING RV=1 GET GARBAGE.", C_GREY);
    text(8, 202, "ALL 0 = THE SH2S ARE HELD. 9 = CODE", C_GREY);
    flip();
}

static int block(int op, uint32_t addr)   /* 1 ok, 0 no answer */
{
    MARS_SYS_COMM8 = (short)(addr >> 16);
    MARS_SYS_COMM10 = (short)addr;
    return md_cmd((uint16_t)(0x1400 | op), 0);
}

static void copy_probe_fn(void)
{   /* the 68000 pokes probe_fn (256 bytes is plenty) into bank 4 at 0x70000 through window 4 (0x1300) */
    const uint16_t *src = (const uint16_t *)(((uint32_t)probe_fn & 0x00FFFFFFu) | 0x02000000u);
    uint32_t dst = (4u << 19) + 0x70000u;
    for (int i = 0; i < 128; i++)
    {
        COMM8_32 = dst + 2u * (uint32_t)i;
        if (!md_cmd(0x1300, src[i])) fail_stop("68000 NOT ANSWERING (CODE COPY)");
    }
}

int main(void)
{
    MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256;
    palette();
    for (int i = 0; i < 2; i++) { line_table(); clear(); flip(); }
    draw();

    if (!md_cmd(0x0600, 0x8000)) fail_stop("68000 NOT ANSWERING");      /* W off, banks 0-7; the fills
                                                                           and the poke turn W on inside RV = 1 */
    phase = "FILL W4 W2"; draw();
    if (!md_cmd(0x0704, 0x4444)) fail_stop("68000 NOT ANSWERING (FILL 4)");
    if (!md_cmd(0x0702, 0x2222)) fail_stop("68000 NOT ANSWERING (FILL 2)");
    phase = "CODE -> W4"; draw();
    copy_probe_fn();

    for (int cur = 0; cur < 9; cur++)
    {
        Phase *p = &ph[cur];
        phase = p->name; draw();
        slave_mode = p->code;
        master_hammer = p->master;
        master_bad = 0;
        hammer_on = p->slave;
        if (p->slave) { uint32_t t0 = gTicks; while (gTicks - t0 < 30) { } }   /* let the slave get going */
        uint32_t s_bad0 = slave_bad, s_reads0 = slave_reads;
        uint32_t t0 = gTicks;
        int ok = 1;
        if (p->op < 0)
        {
            while (gTicks - t0 < 60) { }
        }
        else
        {
            if (!md_cmd((uint16_t)(0x0600 | p->win), (uint16_t)p->bank)) fail_stop("68000 NOT ANSWERING (CTRL)");
            for (int n = 0; n < REPEAT && ok; n++)
                ok = block(p->op, p->addr);
            if (ok && !md_cmd((uint16_t)(0x0600 | p->win), (uint16_t)p->win)) fail_stop("68000 NOT ANSWERING (CTRL BACK)");
        }
        p->ticks = gTicks - t0;
        hammer_on = 0;
        master_hammer = 0;
        { uint32_t t1 = gTicks; while (gTicks - t1 < 5) { } }                /* the slave finishes its pass */
        p->s_bad = slave_bad - s_bad0;
        p->s_reads = slave_reads - s_reads0;
        p->m_bad = master_bad;
        if (!ok) { p->state = 3; draw(); break; }
        p->state = (p->s_bad || p->m_bad) ? 2 : 1;
        draw();
    }
    phase = "DONE"; draw();
    while (1) { }
}
