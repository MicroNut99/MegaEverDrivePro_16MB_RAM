/* edw0 - the EverDrive PRO's window 0 as the SH2 reads it (CD32X BootCamp, 25 Oct 2026).
 *
 * edtest proved banks 1-27 through windows 1-7 (0x080000-0x3FFFFF) for the 68000 and both SH2s.  It never
 * read window 0 (0x000000-0x07FFFF, the SH2's 0x02000000: the program's own bank).  The cartridge boot
 * puts the game there, and on the console the SH2 image copied out of it is corrupt (BootCampDiagT4).
 *
 * This ROM: the 68000 maps bank 0 into window 1 and fills bank 0's upper half (0x40000-0x7FFFF, above
 * this program) with a pattern, then the master SH2 reads it back through WINDOW 0, cache-through:
 *   A  with the 68000 idle;          B  while the 68000 reads window 0 itself (as its vectors do);
 *   C  bank 8 through window 1 (control, 68000 idle);   D  the same while the 68000 reads window 0;
 *   E  window 0 again, cached (as the game's code is fetched).
 * Error counts accumulate over passes; the first bad word of each phase is shown.
 * Only operations edtest ran on the console: mapper writes (RV = 1), 68000 fills, SH2 reads.
 */
#include <stdint.h>
#include "32x.h"
#include "font8x8.h"

#ifndef ED_CTRL0
#define ED_CTRL0 0xE000
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

/* ---- the slave's job: verify the second half of a window (shared through cache-through SDRAM) ---- */

static uint16_t pat(uint32_t i, uint16_t seed)
{
    return (uint16_t)((i & 0xFFFF) ^ (((i >> 16) & 3) << 12) ^ seed);
}

static void verify_range(int window, uint16_t seed, uint32_t from, uint32_t to,
                         uint32_t *bad, uint32_t *first, uint32_t *got)
{
    const volatile uint16_t *w = (const volatile uint16_t *)(0x22000000u + ((uint32_t)window << 19));
    uint32_t n = 0, f = 0xFFFFFFFFu, g = 0;
    for (uint32_t i = from; i < to; i++)
    {
        uint16_t v = w[i];
        if (v != pat(i, seed))
        {
            if (!n) { f = i * 2; g = v; }
            n++;
        }
    }
    *bad = n; *first = f; *got = g;
}

void sec_dma1_handler(void) { }
void secondary(void) { while (1) { } }

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
typedef struct { uint32_t bad, passes_bad, first, want, got; const char *name; } Phase;
static Phase ph[8] = {
    { 0, 0, 0, 0, 0, "8000 W=0 68K WRITES RAM  " },
    { 0, 0, 0, 0, 0, "8000 W=0 68K VBLANK IRQS " },
    { 0, 0, 0, 0, 0, "8000 W=0 68K WRITES GATE " },
    { 0, 0, 0, 0, 0, "8000 W=0 IDLE (DAMAGE?)  " },
    { 0, 0, 0, 0, 0, "A000 W=1 IDLE (W 0>1 OK?)" },
    { 0, 0, 0, 0, 0, "A000 W=1 68K WRITES RAM  " },
    { 0, 0, 0, 0, 0, "A000 W=1 IDLE (DAMAGE?)  " },
    { 0, 0, 0, 0, 0, "8000 W=0 68K VBLANK CACHED" },
};
static uint32_t pass_no;
static const char *phase = "START";

static void fail_stop(const char *why)
{
    text(8, 200, why, C_RED); flip();
    while (1) { }
}

static void note(Phase *p, uint32_t bad, uint32_t first, uint16_t want, uint32_t got)
{
    p->bad += bad;
    if (bad) { p->passes_bad++; p->first = first; p->want = want; p->got = got; }
}

static void verify_cached(int window, uint16_t seed, uint32_t from, uint32_t to,
                          uint32_t *bad, uint32_t *first, uint32_t *got)
{
    const volatile uint16_t *w = (const volatile uint16_t *)(0x02000000u + ((uint32_t)window << 19));
    uint32_t n = 0, f = 0xFFFFFFFFu, g = 0;
    *(volatile uint8_t *)0xFFFFFE92 = 0x11;              /* purge the cache, keep it on */
    for (uint32_t i = from; i < to; i++)
    {
        uint16_t v = w[i];
        if (v != pat(i, seed))
        {
            if (!n) { f = i * 2; g = v; }
            n++;
        }
    }
    *bad = n; *first = f; *got = g;
}

static void draw(void)
{
    char b[16];
    clear();
    text(8, 4, "EDW0  EVERDRIVE WINDOW 0 PROBE", C_YELLOW);
    text(8, 16, "CTRL0", C_GREY); text(56, 16, hexn(b, ED_CTRL0, 4), C_YELLOW);
    text(120, 16, "PASS", C_GREY); text(160, 16, dec(b, pass_no), C_WHITE);
    text(216, 16, "PHASE", C_GREY); text(264, 16, phase, C_WHITE);
    text(8 + 27 * 8, 28, "ERRORS BAD", C_GREY);
    for (int i = 0; i < 8; i++)
    {
        int y = 38 + i * 20;
        Phase *p = &ph[i];
        text(8, y, p->name, p->bad ? C_RED : C_GREEN);
        text(8 + 27 * 8, y, dec(b, p->bad), p->bad ? C_RED : C_WHITE);
        text(8 + 34 * 8, y, dec(b, p->passes_bad), C_WHITE);
        if (p->bad)
        {
            text(16, y + 10, "+", C_GREY); text(24, y + 10, hexn(b, p->first, 5), C_WHITE);
            text(72, y + 10, hexn(b, p->want, 4), C_WHITE); text(112, y + 10, hexn(b, p->got, 4), C_RED);
        }
    }
    text(8, 200, "W = THE EVERDRIVE'S WRITE-ENABLE BIT (CTRL0).", C_GREY);
    text(8, 210, "IDLE ROWS: IS THE PATTERN STILL INTACT?", C_GREY);
    flip();
}

static void hammer_start(void) { MARS_SYS_COMM2 = 120; MARS_SYS_COMM0 = 0x0B00; }   /* ~1 s of 68000 reads */
static void hammer_start_cmd(uint16_t cmd) { MARS_SYS_COMM2 = 120; MARS_SYS_COMM0 = cmd; }
static void hammer_wait(void)  { uint32_t t0 = gTicks; while (MARS_SYS_COMM0 != 0 && gTicks - t0 < 600) { } }

int main(void)
{
    uint32_t bad, first, got;
    const uint16_t seed0 = 0x1234, seed8 = 0x5A5A;
    const uint32_t FROM = 131072, TO = 262144;          /* words: 0x40000-0x7FFFF of the window */

    MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256;
    palette();
    for (int i = 0; i < 2; i++) { line_table(); clear(); flip(); }
    draw();

    if (!md_cmd(0x0600, ED_CTRL0)) fail_stop("68000 NOT ANSWERING");
    phase = "FILL B0";  draw();
    if (!md_cmd(0x0601, 0)) fail_stop("68000 NOT ANSWERING");          /* window 1 = bank 0 */
    if (!md_cmd(0x0901, seed0)) fail_stop("68000 NOT ANSWERING");      /* bank 0's upper half */
    phase = "FILL B8";  draw();
    if (!md_cmd(0x0601, 8)) fail_stop("68000 NOT ANSWERING");          /* window 1 = bank 8 */
    if (!md_cmd(0x0901, seed8)) fail_stop("68000 NOT ANSWERING");
    if (!md_cmd(0x0601, 1)) fail_stop("68000 NOT ANSWERING");          /* window 1 = bank 1 again */

    while (1)
    {
        pass_no++;
        if (!md_cmd(0x0600, 0x8000)) fail_stop("68000 NOT ANSWERING");       /* W = 0 */
        phase = "1"; draw();
        hammer_start_cmd(0x1100); verify_range(0, seed0, FROM, TO, &bad, &first, &got); hammer_wait();
        note(&ph[0], bad, first, pat(first / 2, seed0), got);
        phase = "2"; draw();
        hammer_start_cmd(0x0F00); verify_range(0, seed0, FROM, TO, &bad, &first, &got); hammer_wait();
        note(&ph[1], bad, first, pat(first / 2, seed0), got);
        phase = "3"; draw();
        hammer_start_cmd(0x1200); verify_range(0, seed0, FROM, TO, &bad, &first, &got); hammer_wait();
        note(&ph[2], bad, first, pat(first / 2, seed0), got);
        phase = "4"; draw();
        verify_range(0, seed0, FROM, TO, &bad, &first, &got); note(&ph[3], bad, first, pat(first / 2, seed0), got);
        if (!md_cmd(0x0600, 0xA000)) fail_stop("68000 NOT ANSWERING");       /* W = 1 */
        phase = "5"; draw();
        verify_range(0, seed0, FROM, TO, &bad, &first, &got); note(&ph[4], bad, first, pat(first / 2, seed0), got);
        phase = "6"; draw();
        hammer_start_cmd(0x1100); verify_range(0, seed0, FROM, TO, &bad, &first, &got); hammer_wait();
        note(&ph[5], bad, first, pat(first / 2, seed0), got);
        phase = "7"; draw();
        verify_range(0, seed0, FROM, TO, &bad, &first, &got); note(&ph[6], bad, first, pat(first / 2, seed0), got);
        if (!md_cmd(0x0600, 0x8000)) fail_stop("68000 NOT ANSWERING");       /* W = 0 */
        phase = "8"; draw();
        hammer_start_cmd(0x0F00); verify_cached(0, seed0, FROM, TO, &bad, &first, &got); hammer_wait();
        note(&ph[7], bad, first, pat(first / 2, seed0), got);
        draw();
    }
}
