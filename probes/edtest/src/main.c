/* edtest - Mega EverDrive PRO as a 16 MB 32X RAM cart: a test (CD32X BootCamp)
 *
 * A 32X cartridge ROM with "SEGA SSF" in its header, run from the EverDrive menu with the 32X attached
 * (no CD needed).  It turns on the EverDrive's extended SSF mapper with its write bit, then, pass after pass:
 *   1. maps EverDrive banks 1-27 (512 KB each, 13.5 MB) four groups at a time into the cart windows
 *      0x080000-0x3FFFFF (mapper registers CTRL1-7), and has the 68000 fill each with a pattern of its own
 *      (with RV = 1, as the CD32X stack writes its RAM cart), checking it at once;
 *   2. after ALL banks are written, maps each one in again and checks it with the 68000 AND with both SH2s
 *      (cache-through reads at 0x22000000, as a game reads its cart): a bank that only kept its data while it
 *      was mapped, or that shares memory with another bank, shows up here.
 * Bank 0 (this program) and banks 28-31 (the EverDrive's save area and OS) are never written.
 *
 * Per bank:  OK   = written, kept, and read back the same by the 68000 and both SH2s
 *            W    = not writable (or not mapped): it did not read back right after writing
 *            A    = written, but lost or overwritten after other banks were mapped in (aliasing)
 *            S    = the 68000 reads it right, an SH2 does not
 * The first error shows its bank, offset, the word expected and the word read, and who read it.
 *
 * CTRL0 (0xA130F0) is written as ED_CTRL0 (Makefile): 0xE000 = P + X (32X mode) + W (write), per the
 * EverDrive X-series notes; 0xA000 = P + W, as the Pro write-ups show it.  Two ROMs are built.
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
typedef struct { uint32_t go, seq, window, seed, bad, first, got, done_seq; } Job;
static Job job_mem;
#define JOB ((volatile Job *)((uint32_t)&job_mem | 0x20000000u))

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
void secondary(void)                        /* the slave SH2: verifies words 131072-262143 of a window */
{
    while (1)
    {
        while (JOB->go != 1) { }
        uint32_t bad, first, got;
        verify_range((int)JOB->window, (uint16_t)JOB->seed, WORDS / 2, WORDS, &bad, &first, &got);
        JOB->bad = bad; JOB->first = first; JOB->got = got;
        JOB->done_seq = JOB->seq;           /* the master accepts only the answer to its own job */
        JOB->go = 2;
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

/* ---- the results ---- */
enum { ST_UNTESTED, ST_OK, ST_W, ST_A, ST_S };
static uint8_t  status[32];
static uint32_t bad68_total, badsh2_total, pass_no;
static int      have_first;
static uint32_t first_bank, first_off, first_want, first_got;
static const char *first_who = "";
static const char *phase = "START";
static int      cur_bank;

static void note_first(int bank, uint32_t off, uint16_t want, uint16_t got, const char *who)
{
    if (have_first) return;
    have_first = 1;
    first_bank = (uint32_t)bank; first_off = off; first_want = want; first_got = got; first_who = who;
}

static uint16_t nonce;                      /* new every run: a pattern left in the EverDrive's RAM by an
                                               earlier run (it keeps its RAM across a return to the menu)
                                               can never pass for this run's writes */
static uint16_t seed_of(int bank, uint32_t pass)
{
    uint16_t s = (uint16_t)(bank * 0x1F3D) ^ (uint16_t)(pass * 0xA5A5) ^ nonce;
    return (pass & 1) ? (uint16_t)~s : s;   /* every other pass: the bits inverted */
}

static const char *mapper_text = "NOT CHECKED";
static int      mapper_ok;
static int      slave_dead;
static uint16_t stuck_comm[8];
static int      stuck;

static void draw(void)
{
    char b[16];
    clear();
    text(8, 4, "EVERDRIVE PRO RAM TEST  CTRL0", C_YELLOW);
    text(248, 4, hexn(b, ED_CTRL0, 4), C_YELLOW);
    text(8, 16, "PASS", C_GREY); text(48, 16, dec(b, pass_no), C_WHITE);
    text(80, 16, phase, C_CYAN);
    if (cur_bank && !stuck) { text(200, 16, "BANK", C_GREY); text(240, 16, dec(b, (uint32_t)cur_bank), C_WHITE); }
    text(280, 16, dec(b, gTicks / 60), C_GREY);

    for (int bank = FIRST_BANK; bank <= LAST_BANK; bank++)
    {
        int k = bank - FIRST_BANK, x = 8 + (k / 9) * 104, y = 32 + (k % 9) * 11;
        b[0] = 'B'; b[1] = (char)('0' + bank / 10); b[2] = (char)('0' + bank % 10); b[3] = 0;
        text(x, y, b, C_GREY);
        static const char *const st[] = { "-", "OK", "W", "A", "S" };
        static const uint8_t col[] = { C_GREY, C_GREEN, C_RED, C_RED, C_RED };
        text(x + 40, y, st[status[bank]], col[status[bank]]);
    }

    text(8, 134, "BAD WORDS 68K", C_GREY);  text(120, 134, dec(b, bad68_total), C_WHITE);
    text(208, 134, "SH2", C_GREY);           text(240, 134, dec(b, badsh2_total), C_WHITE);
    if (have_first)
    {
        text(8, 148, "FIRST ERROR: BANK", C_RED); text(152, 148, dec(b, first_bank), C_WHITE);
        text(176, 148, first_who, C_WHITE);
        text(8, 159, "OFFSET +", C_GREY); text(72, 159, hexn(b, first_off, 5), C_WHITE);
        text(128, 159, "WANT", C_GREY);   text(168, 159, hexn(b, first_want, 4), C_WHITE);
        text(208, 159, "GOT", C_GREY);    text(240, 159, hexn(b, first_got, 4), C_WHITE);
    }
    int ok = 0, w = 0, a = 0, s = 0, done = 0;
    for (int bank = FIRST_BANK; bank <= LAST_BANK; bank++)
    {
        done += status[bank] != ST_UNTESTED;
        ok += status[bank] == ST_OK; w += status[bank] == ST_W; a += status[bank] == ST_A; s += status[bank] == ST_S;
    }
    const char *v = !done ? "TESTING..." :
                    w == done ? "NOT WRITABLE WITH THIS CTRL0" :
                    a ? "BANKS LOSE DATA OR ALIAS" :
                    s ? "SH2 READS DIFFER FROM 68000" :
                    w ? "SOME BANKS NOT WRITABLE" : "ALL TESTED BANKS OK";
    text(8, 167, "MAPPER:", C_GREY);           /* y 167: under the OFFSET line (159-166) of an error */
    text(72, 167, mapper_text, mapper_ok ? C_GREEN : C_RED);
    text(248, 167, "RUN", C_GREY); text(280, 167, hexn(b, nonce, 4), C_GREY);
    if (slave_dead) text(208, 148, "SLAVE SH2 NO ANSWER", C_RED);
    text(8, 176, "VERDICT:", C_GREY); text(80, 176, v, (ok == done && done) ? C_GREEN : C_YELLOW);
    if (stuck)                              /* which 68000 command did not come back, and the COMM registers */
    {
        text(8, 186, "CMD", C_RED); text(40, 186, hexn(b, last_cmd, 4), C_WHITE);
        text(88, 186, "ARG", C_RED); text(120, 186, hexn(b, last_arg, 4), C_WHITE);
        for (int i = 0; i < 8; i++)
            text(8 + (i & 3) * 80, 196 + (i >> 2) * 10, hexn(b, stuck_comm[i], 4), C_WHITE);
        flip();
        return;
    }
    text(8, 194, "W=NOT WRITABLE A=LOST S=SH2 BAD", C_GREY);
    text(8, 206, "BANKS 0 AND 28-31 ARE NEVER WRITTEN", C_GREY);
    flip();
}

static void fail_stop(const char *why)
{
    phase = why;
    stuck = 1;
    for (int i = 0; i < 8; i++)             /* COMM0 ... COMM14 when the 68000 stopped answering */
        stuck_comm[i] = ((volatile uint16_t *)0x20004020)[i];
    while (1) draw();
}

int main(void)
{
    MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256;
    palette();
    for (int i = 0; i < 2; i++) { line_table(); clear(); flip(); }
    draw();

    /* this run's number: the SH2's free-running timer and the time it took to get here */
    nonce = (uint16_t)((*(volatile uint8_t *)0xFFFFFE12 << 8) | *(volatile uint8_t *)0xFFFFFE13) ^ (uint16_t)(gTicks * 0x9E37);
    if (!nonce) nonce = 0x1234;

    /* CTRL0: protection + (32X) + write, bank 0 stays in window 0 (this program) */
    if (!md_cmd(0x0600, ED_CTRL0)) fail_stop("68000 NOT ANSWERING");

    /* does the mapper switch banks at all?  Map bank 0 (this ROM) into window 1 as well: the two windows
       must then read the same; then bank 1 into window 1: they must differ.  Checked over 16 KB. */
    {
        const volatile uint16_t *w0 = (const volatile uint16_t *)0x22000000u;
        const volatile uint16_t *w1 = (const volatile uint16_t *)0x22080000u;
        int same0 = 1, same1 = 1;
        if (!md_cmd(0x0601, 0)) fail_stop("68000 NOT ANSWERING");
        for (int i = 0; i < 8192; i++) if (w0[i] != w1[i]) { same0 = 0; break; }
        if (!md_cmd(0x0601, 1)) fail_stop("68000 NOT ANSWERING");
        for (int i = 0; i < 8192; i++) if (w0[i] != w1[i]) { same1 = 0; break; }
        mapper_ok = same0 && !same1;
        mapper_text = mapper_ok ? "SWITCHES BANKS" : same0 ? "NO CHANGE AFTER CTRL1 WRITE" : "DOES NOT RESPOND";
    }

    for (pass_no = 1;; pass_no++)
    {
        /* 1. write every bank, checking each at once */
        phase = "WRITE";
        for (int bank = FIRST_BANK; bank <= LAST_BANK; bank++)
        {
            int w = 1 + (bank - FIRST_BANK) % 7;        /* window 1-7 = cart 0x080000 x w */
            uint16_t seed = seed_of(bank, pass_no);
            cur_bank = bank;
            draw();
            if (!md_cmd((uint16_t)(0x0600 | w), (uint16_t)bank)) fail_stop("68000 NOT ANSWERING");
            if (!md_cmd((uint16_t)(0x0700 | w), seed)) fail_stop("68000 NOT ANSWERING");
            if (!md_cmd((uint16_t)(0x0800 | w), seed)) fail_stop("68000 NOT ANSWERING");
            if (COMM8_32)
            {
                status[bank] = ST_W;
                bad68_total += COMM8_32;
                uint32_t off = COMM12_32;
                note_first(bank, off, pat(off / 2, seed), (uint16_t)MARS_SYS_COMM2, "68K AT ONCE");
            }
        }
        /* 2. read every bank back, after all the others were written */
        phase = "VERIFY";
        for (int bank = FIRST_BANK; bank <= LAST_BANK; bank++)
        {
            int w = 1 + (bank - FIRST_BANK) % 7;
            uint16_t seed = seed_of(bank, pass_no);
            cur_bank = bank;
            draw();
            if (!md_cmd((uint16_t)(0x0600 | w), (uint16_t)bank)) fail_stop("68000 NOT ANSWERING");
            if (!md_cmd((uint16_t)(0x0800 | w), seed)) fail_stop("68000 NOT ANSWERING");
            uint32_t bad68 = COMM8_32, off68 = COMM12_32;
            uint16_t got68 = (uint16_t)MARS_SYS_COMM2;

            static uint32_t seq;
            JOB->window = (uint32_t)w; JOB->seed = seed; JOB->seq = ++seq; JOB->go = 1;  /* the slave: second half */
            uint32_t badm, firstm, gotm;
            verify_range(w, seed, 0, WORDS / 2, &badm, &firstm, &gotm);
            uint32_t t0 = gTicks;
            while (JOB->go != 2 && gTicks - t0 < 600) { }
            int slave_ok = JOB->go == 2 && JOB->done_seq == seq;
            uint32_t bads = slave_ok ? JOB->bad : 0, firsts = JOB->first, gots = JOB->got;
            if (!slave_ok) slave_dead = 1;
            JOB->go = 0;

            if (status[bank] == ST_W) { }
            else if (bad68)
            {
                status[bank] = ST_A;
                note_first(bank, off68, pat(off68 / 2, seed), got68, "68K LATER");
            }
            else if (badm || bads)
            {
                status[bank] = ST_S;
                if (badm) note_first(bank, firstm, pat(firstm / 2, seed), (uint16_t)gotm, "MASTER SH2");
                else      note_first(bank, firsts, pat(firsts / 2, seed), (uint16_t)gots, "SLAVE SH2");
            }
            else if (status[bank] == ST_UNTESTED)
                status[bank] = ST_OK;
            bad68_total += bad68;
            badsh2_total += badm + bads;
        }
        cur_bank = 0;
        phase = "PASS DONE";
        draw();
    }
    return 0;
}
