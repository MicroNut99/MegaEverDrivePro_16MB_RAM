/* edsave - does the EverDrive PRO keep what a game stores? (CD32X BootCamp, 25 Oct 2026)
 *
 * OpenLara's saves live in the RAM cart: the slots at cart 0x060000 (bank 0) and the settings at
 * 0x3F0000 (bank 7).  Banks 0-29 are the EverDrive's 16 MB PSRAM; bank 31 is its battery RAM, the
 * memory the EverDrive OS syncs with a .srm file on the SD card (krikzz's SSF mapper source: bank 31
 * -> bram).  This ROM writes a 32-byte marker at twelve places - bank 0, bank 7, bank 31 (first 256 KB
 * only: the OS keeps its own things above) - and on every start first reports what it finds there:
 *   NEW            nothing of ours                RUN n OK    the marker run n left, intact
 *   RUN n BAD k    the marker is there but k of its 12 pattern words changed
 * then writes run n+1 everywhere and reads it back (WROTE OK / WROTE BAD).
 * Run it, reset to the menu, run it again: the second screen says which banks kept the markers.
 * The ROM declares save RAM in its header (ED_SRAM=1), so the OS should keep EDSAVE1.srm on the SD card.
 * Only console-proven operations: mapper writes (RV = 1), 68000 word writes (RV = 1, W = 1 while the
 * SH2 does not read the cart), SH2 reads with W = 0.
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
#define MAGIC0  0x4553u                 /* 'ES' */
#define MAGIC1  0x5631u                 /* 'V1' */
#define NWORDS  16

typedef struct {
    const char *name;
    uint32_t cart;                      /* cart address (0..0x3FFFFF) as the SH2 reads it, or the offset in the bank */
    int bank;                           /* -1: a fixed window (bank 0-7 as mapped at start); else this bank through window 1 */
    int state;                          /* 0 new, 1 ok, 2 bad */
    uint32_t run, badw, wrote;          /* wrote: 1 verified, 2 mismatch */
    uint16_t seen;                      /* the first word read back after the write */
} Site;

static Site site[12] = {
    { "B0  020000", 0x020000, -1 }, { "B0  040000", 0x040000, -1 }, { "B0  060000 SLOT", 0x060000, -1 },
    { "B0  07FFE0", 0x07FFE0, -1 },
    { "B7  3F0000 SET ", 0x3F0000, -1 }, { "B7  3F0400 SAVE", 0x3F0400, -1 }, { "B7  3FFFE0", 0x3FFFE0, -1 },
    { "B31 000000", 0x000000, 31 }, { "B31 007FE0 32K ", 0x007FE0, 31 }, { "B31 00FFE0 64K ", 0x00FFE0, 31 },
    { "B31 01FFE0 128K", 0x01FFE0, 31 }, { "B31 03FFE0 256K", 0x03FFE0, 31 },
};
static const char *phase = "START";
static uint32_t next_run;

static void fail_stop(const char *why)
{
    text(8, 200, why, C_RED); flip();
    while (1) { }
}

static uint16_t pat(const Site *s, uint32_t run, int i)
{
    return (uint16_t)((((s->cart >> 1) + (uint32_t)i) * 0x9E37u) ^ run ^ 0xA5A5u);
}

/* where the SH2 reads a site: a bank through window 1 (0x080000), or the fixed windows */
static volatile uint16_t *sh2_addr(const Site *s)
{
    uint32_t a = (s->bank < 0) ? s->cart : 0x080000u + s->cart;
    return (volatile uint16_t *)(0x22000000u + a);
}

static void map_window1(int bank)
{
    if (!md_cmd(0x0601, (uint16_t)bank)) fail_stop("68000 NOT ANSWERING");
}

static void inspect(Site *s)
{
    volatile uint16_t *w = sh2_addr(s);
    if (w[0] != MAGIC0 || w[1] != MAGIC1) { s->state = 0; s->run = 0; s->badw = 0; return; }
    s->run = w[2];
    s->badw = 0;
    for (int i = 4; i < NWORDS; i++)
        if (w[i] != pat(s, s->run, i)) s->badw++;
    s->state = s->badw ? 2 : 1;
}

/* the 68000 writes a word: 0x1300, COMM8:COMM10 = cart address, COMM2 = word */
static void poke(uint32_t cart, uint16_t v)
{
    MARS_SYS_COMM8 = (short)(cart >> 16);
    MARS_SYS_COMM10 = (short)cart;
    if (!md_cmd(0x1300, v)) fail_stop("68000 NOT ANSWERING");
}

static void write_site(Site *s, uint32_t run)
{
    /* window 0 cannot be written directly: bank 0 goes through window 1 (the cartridge boot's route) */
    uint32_t a = (s->bank < 0) ? s->cart : 0x080000u + s->cart;
    if (s->bank < 0 && s->cart < 0x080000) a += 0x080000;
    uint16_t v[NWORDS];
    v[0] = MAGIC0; v[1] = MAGIC1; v[2] = (uint16_t)run; v[3] = (uint16_t)(s - site);
    for (int i = 4; i < NWORDS; i++) v[i] = pat(s, run, i);
    for (int i = 0; i < NWORDS; i++) poke(a + 2 * i, v[i]);
}

static void verify_site(Site *s, uint32_t run)
{
    volatile uint16_t *w = sh2_addr(s);
    s->wrote = 1;
    s->seen = w[0];
    if (w[0] != MAGIC0 || w[1] != MAGIC1 || w[2] != run) s->wrote = 2;
    for (int i = 4; i < NWORDS; i++)
        if (w[i] != pat(s, run, i)) s->wrote = 2;
}

static void draw(void)
{
    char b[16];
    clear();
    text(8, 4, "EDSAVE1  WHAT DOES THE EVERDRIVE KEEP?", C_YELLOW);
    text(8, 16, "PHASE", C_GREY); text(56, 16, phase, C_WHITE);
    text(8, 28, "SITE", C_GREY); text(144, 28, "AT START", C_GREY); text(256, 28, "NOW", C_GREY);
    for (int i = 0; i < 12; i++)
    {
        Site *s = &site[i];
        int y = 40 + i * 11;
        text(8, y, s->name, C_WHITE);
        if (s->state == 0) text(144, y, "NEW", C_GREY);
        else
        {
            text(144, y, "RUN", s->state == 1 ? C_GREEN : C_RED);
            text(176, y, dec(b, s->run), s->state == 1 ? C_GREEN : C_RED);
            if (s->state == 1) text(200, y, "OK", C_GREEN);
            else { text(200, y, "BAD", C_RED); text(232, y, dec(b, s->badw), C_RED); }
        }
        if (s->wrote == 1) text(256, y, "OK", C_GREEN);
        if (s->wrote == 2) { text(256, y, "BAD", C_RED); text(288, y, hexn(b, s->seen, 4), C_RED); }
    }
    text(8, 176, "THIS RUN WROTE RUN", C_GREY); text(160, 176, dec(b, next_run), C_YELLOW);
    text(8, 190, "NOW: RESET TO MENU, START IT AGAIN", C_CYAN);
    text(8, 200, "B0/B7 KEPT: PSRAM SURVIVES THE MENU", C_GREY);
    text(8, 210, "B31 KEPT + EDSAVE1.SRM ON SD: SD SAVE", C_GREY);
    flip();
}

int main(void)
{
    MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256;
    palette();
    for (int i = 0; i < 2; i++) { line_table(); clear(); flip(); }
    draw();

    /* W off, banks 0-7 in windows 0-7 (as the menu leaves them; CTRL0 also sets window 0 = bank 0) */
    if (!md_cmd(0x0600, 0x8000)) fail_stop("68000 NOT ANSWERING");
    phase = "LOOK"; draw();
    uint32_t run_max = 0;
    for (int i = 0; i < 12; i++)
    {
        Site *s = &site[i];
        if (s->bank >= 0) map_window1(s->bank);
        inspect(s);
        if (s->bank >= 0) map_window1(1);
        if (s->state && s->run > run_max) run_max = s->run;
    }
    next_run = run_max + 1;
    draw();

    /* write: W on, nothing else from the 68000 but the pokes and the mapper, the SH2 off the cart */
    phase = "WRITE"; draw();
    if (!md_cmd(0x0600, 0xA000)) fail_stop("68000 NOT ANSWERING");
    for (int i = 0; i < 12; i++)
    {
        Site *s = &site[i];
        int w1 = (s->bank >= 0) ? s->bank : (s->cart < 0x080000 ? 0 : -1);
        if (w1 >= 0) map_window1(w1);
        write_site(s, next_run);
        if (w1 >= 0) map_window1(1);
    }
    if (!md_cmd(0x0600, 0x8000)) fail_stop("68000 NOT ANSWERING");

    phase = "CHECK"; draw();
    for (int i = 0; i < 12; i++)
    {
        Site *s = &site[i];
        if (s->bank >= 0) map_window1(s->bank);
        verify_site(s, next_run);
        if (s->bank >= 0) map_window1(1);
    }
    phase = "DONE"; draw();
    while (1) { }
}
