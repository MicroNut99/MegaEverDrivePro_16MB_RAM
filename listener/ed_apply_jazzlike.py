#!/usr/bin/env python3
"""ed_apply_jazzlike.py <game folder> [--subcpu cd32x/subcpu] [--engine-make mars/Makefile] [--cart-var CART]

The EverDrive switches for a game built the OpenJazz way (port/plat_mars.c, port/cartcache.c, a
mars/Makefile with CART=8/16, a build.sh with the same): CART=ED, EDCD=1 and EDSAVE-4.  Idempotent:
each edit is skipped when its marker is already there.  Prints what it did; stops on the first
text it cannot find (then do that one by hand).  Run from the game folder's parent or anywhere.
"""
import os, re, sys

def edit(path, old, new, marker=None, count=1):
    s = open(path).read()
    if marker and marker in s:
        print(f"  {os.path.basename(path)}: already ({marker})"); return
    if s.count(old) != count:
        print(f"*** {path}: expected {count} of:\n{old[:200]}"); sys.exit(1)
    open(path, "w").write(s.replace(old, new)); print(f"  {os.path.basename(path)}: ok")

game = os.path.abspath(sys.argv[1])
args = dict(zip(sys.argv[2::2], sys.argv[3::2]))
subcpu = os.path.join(game, args.get("--subcpu", "cd32x/subcpu"))
emake = os.path.join(game, args.get("--engine-make", "mars/Makefile"))
here = os.path.dirname(os.path.abspath(__file__))
stack = os.path.join(here, "..", "stack", "subcpu", "hw_md.s")

print("== cartcache.c: the probe stops at page 6 on the EverDrive")
p = os.path.join(game, "port/cartcache.c")
edit(p, """static int probe(void)
{
	int wrote[8] = { 0 }, good[8] = { 0 };
	for (int p = 7; p >= 1; p--) {""", """/* ED (OJ32X_CART_ED, the Mega EverDrive PRO as the cart - CD32X BootCamp docs/12): page 7 would be
 * the EverDrive's banks 28-31, and bank 31 is its battery RAM, the OS's own.  Pages 1-6 only. */
#ifdef OJ32X_CART_ED
#define PROBE_TOP 6
#else
#define PROBE_TOP 7
#endif
static int probe(void)
{
	int wrote[8] = { 0 }, good[8] = { 0 };
	for (int p = PROBE_TOP; p >= 1; p--) {""", "PROBE_TOP")
s = open(p).read()
if "p <= PROBE_TOP" not in s:
    s = s.replace("""	for (int p = 1; p <= 7; p++) {
		uint16_t w[3];""", """	for (int p = 1; p <= PROBE_TOP; p++) {
		uint16_t w[3];""")
    s = s.replace("""	for (int p = 1; p <= 7; p++) all &= good[p];
	int n = all ? 8 : low ? 4 : 2;""", """	for (int p = 1; p <= PROBE_TOP; p++) all &= good[p];
	int n = all ? PROBE_TOP + 1 : low ? 4 : 2;      /* ED: 7 = pages 1-6 */""")
    assert s.count("PROBE_TOP") == 6, s.count("PROBE_TOP")
    open(p, "w").write(s); print("  cartcache.c: loops ok")

print("== plat_mars.c: EDSAVE-4 (both SH2s in SDRAM for every RV = 1 command)")
p = os.path.join(game, "port/plat_mars.c")
edit(p, "typedef struct { void (*fn)(void *, int); void *arg; int go; int done; } SlaveJob;",
     "typedef struct { void (*fn)(void *, int); void *arg; int go; int done; int park, parked; } SlaveJob;   /* ED: park (EDSAVE-4) */",
     "int park, parked;")
s = open(p).read()
if "sdram_wait" not in s:
    m = re.search(r"\nvoid secondary\(void\)\n\{\n\tfor \(;;\)\n\t\{\n(\t\twhile \(!JOB->go\)[^\n]*\n)", s)
    if not m: print("*** plat_mars.c: the slave loop not found"); sys.exit(1)
    loop = m.group(1)
    newloop = """\t\twhile (!JOB->go) {
\t\t\tif (JOB->park) sdram_wait(NULL, &JOB->park, &JOB->parked);   /* ED: off the cart (EDSAVE-4) */
\t\t\tfor (volatile int k = 0; k < 64; k++) {}
\t\t}
"""
    helper = """
/* ED / EDSAVE-4 (OpenLara CD32X on the Mega EverDrive PRO, 26 Oct 2026; CD32X BootCamp docs/12): on the
 * EverDrive an SH2's cart access while the 68000 holds RV = 1 is NOT held until RV = 0 - it is
 * answered at once with garbage (edsave3: ~100% bad words through every such copy).  So for every
 * command whose 68000 side copies with RV = 1 (chunks, the page, the save area) both SH2s wait in
 * this loop, which lives in SDRAM (.data.sdram), with their interrupts masked: nothing fetches or
 * reads the cart until the Sub-CPU reports the copy done.  A cached loop is luck. */
__attribute__((section(".data.sdram"), noinline))
static void sdram_wait(volatile uint16_t *reg16, volatile int *flag, volatile int *ack)
{	/* reg16: spin while *reg16 != 0 (the master, COMM0); flag: spin while *flag != 0 (the slave,
	   JOB->park), *ack = 1 first.  No calls, no cart data: this function and its stack only. */
	uint32_t sr, masked;
	__asm__ volatile ("stc sr,%0" : "=r"(sr));
	masked = sr | 0xF0u;
	__asm__ volatile ("ldc %0,sr" : : "r"(masked));
	if (ack) *ack = 1;
	for (int32_t n = 0; n < 8000000; n++) {
		if (reg16 ? *reg16 == 0 : *flag == 0) break;
		for (volatile int32_t k = 0; k < 64; k++) {}    /* never hammer a COMM register */
	}
	__asm__ volatile ("ldc %0,sr" : : "r"(sr));
}
"""
    s = s.replace("\nvoid secondary(void)\n", helper + "\nvoid secondary(void)\n", 1)
    s = s.replace(loop, newloop, 1)
    old = """	MARS_COMM0 = cmd;
	for (uint32_t n = 0; n < tries && MARS_COMM0 != 0; n++)
		comm_pause();
	return MARS_COMM0 == 0;
}
"""
    assert s.count(old) == 1
    # whatever send_cmd does before the command (collecting answers sent ahead) send_cmd_rv does too
    i = s.index(old); j = s.rfind("static int send_cmd(uint16_t cmd, uint32_t tries)\n{\n", 0, i)
    pre = s[j + len("static int send_cmd(uint16_t cmd, uint32_t tries)\n{\n"):i]
    s = s.replace(old, old + """
/* ED / EDSAVE-4: a command whose 68000 side holds RV = 1 - the slave parked in SDRAM first (it
 * answers between jobs; a running job finishes first), the master waits in SDRAM too. */
static int send_cmd_rv(uint16_t cmd, uint32_t tries)
{
	(void)tries;
""" + pre + """	JOB->parked = 0;
	JOB->park = 1;
	for (uint32_t n = 0; n < 4000000UL && !JOB->parked; n++) { for (volatile int k = 0; k < 64; k++) {} }
	MARS_COMM0 = cmd;
	sdram_wait(&MARS_COMM0, NULL, NULL);
	JOB->park = 0;
	return MARS_COMM0 == 0;
}
""", 1)
    n = 0
    for name in ["CMD_CHUNK_COMPARE", "CMD_CHUNK_TO_CART", "CMD_UNPACK", "CMD_CART_PAGE", "CMD_SAVE_PIECE_RAW", "CMD_SAVE_PIECE"]:
        k = s.count("send_cmd(%s," % name); n += k
        s = s.replace("send_cmd(%s," % name, "send_cmd_rv(%s," % name)
    open(p, "w").write(s); print(f"  plat_mars.c: ok ({n} commands through send_cmd_rv)")
else:
    print("  plat_mars.c: already")

print("== mars/Makefile: CART=ED")
s = open(emake).read()
if "OJ32X_CART_ED" not in s:
    if "$(if $(CART),-DOJ32X_CART_MB=$(CART))" in s:
        s = s.replace("$(if $(CART),-DOJ32X_CART_MB=$(CART))", "$(if $(CART),-DOJ32X_CART_MB=$(CART_MB)) $(if $(filter ED,$(CART)),-DOJ32X_CART_ED)")
        s = s.replace("OBJDIR := obj$(if $(CART),_cart$(CART))", "# ED: CART=ED - the Mega EverDrive PRO as the 16 MB paged cart (CD32X BootCamp docs/12): 16 MB, pages 1-6\nCART_MB := $(if $(filter ED,$(CART)),16,$(CART))\nOBJDIR := obj$(if $(CART),_cart$(CART))", 1)
        assert "CART_MB :=" in s
        open(emake, "w").write(s); print("  Makefile: ok")
    else:
        print("*** mars/Makefile: the CART define not found"); sys.exit(1)
else:
    print("  Makefile: already")

print("== cd32x/subcpu: the stack's listener (MODE1 switches) + the Makefile block")
import shutil, subprocess
hw = os.path.join(subcpu, "hw_md.s")
if "OL_M1PAGE" not in open(hw).read():
    base = open(hw).read()
    shutil.copy(stack, hw); print("  hw_md.s: the stack's (this game's was the stack's base, no own commands)")
else:
    print("  hw_md.s: already")
mk = os.path.join(subcpu, "Makefile")
edit(mk, "ASFLAGS = -m68000 --register-prefix-optional\n", """ASFLAGS = -m68000 --register-prefix-optional
# MODE1-1 (CD32X BootCamp docs/12): make CDROMPlayer.bin MODE1=1 - the listener for the cartridge boot
# from the Mega EverDrive PRO (hw_md.s OL_MODE1), the program EDCD.32X carries.  Without MODE1:
# unchanged.  build.sh EDCD=1 builds it in a copy of this folder; the disc always gets the normal one.
ifeq ($(MODE1),1)
ASFLAGS += --defsym OL_MODE1=1
ifeq ($(M1WORDS),1)
ASFLAGS += --defsym OL_M1WORDS=1
endif
ifneq ($(M1W0BANK),)
ASFLAGS += --defsym OL_M1W0BANK=$(M1W0BANK)
endif
ifneq ($(M1CTRL0),)
ASFLAGS += --defsym OL_M1CTRL0=$(M1CTRL0)
endif
# MODE1-7: M1SAVE=1 - cart 0x800000-0x83FFFF = the EverDrive's save bank (31, kept on the SD card)
ifeq ($(M1SAVE),1)
ASFLAGS += --defsym OL_M1SAVE=1
endif
# MODE1-9: M1PAGE=1 - the page-register writes (a byte to 0xA130F0/F2) become the EverDrive's window
# registers (page p in window 1 = CTRL4..7 = 4p..4p+3)
ifeq ($(M1PAGE),1)
ASFLAGS += --defsym OL_M1PAGE=1
endif
endif
""", "OL_M1PAGE")

print("== build.sh: CART=ED, EDCD=1")
p = os.path.join(game, "build.sh")
s = open(p).read()
if "EDCD" not in s:
    old = 'case "$CART" in ""|8|16) ;; *) echo "*** CART must be 8 or 16 (or unset) - stopping"; exit 1;; esac\n'
    assert s.count(old) == 1
    s = s.replace(old, '''# ED: CART=ED - the Mega EverDrive PRO as the 16 MB paged cart (CD32X BootCamp docs/12; the listener's
# MODE1-9 turns the page writes into its window registers; pages 1-6 - page 7 is the EverDrive's own).
#   CART=ED EDCD=1 bash build.sh      the disc + cd32x/subcpu/EDCD.32X, the launcher started from the
#                                     EverDrive menu with the disc in the Sega CD (BOOTCAMP=<folder>,
#                                     default ../CD32X-BootCamp)
# Saves still go to the cart's page 1 (the EverDrive wipes its PSRAM at its menu: nothing persists yet).
case "$CART" in ""|8|16|ED) ;; *) echo "*** CART must be 8, 16 or ED (or unset) - stopping"; exit 1;; esac
[ "$CART" = ED ] && echo "==> cart: the Mega EverDrive PRO (16 MB, pages 1-6)"
''')
    m = re.search(r'(echo "==> ROM check";\s*)python3 "([^"]+prep_openlara_cd\.py)" ', s)
    assert m, "ROM check line"
    s = s.replace(m.group(0), 'PREP_MODE1=""; if [ "${EDCD:-0}" = 1 ]; then PREP_MODE1="--mode1"; fi   # MODE1-1 (EDCD=1, below): the RAM jump table too\n' + m.group(1) + 'python3 "' + m.group(2) + '" $PREP_MODE1 ', 1)
    old = 'make cd CART_DEFS="${CART:+-DCART_PAGED}"'
    assert s.count(old) == 1
    s = s.replace(old, 'make cd CART_DEFS="$CART_DEFS"')
    s = s.replace('echo "==> Sub-CPU";', 'CART_DEFS="${CART:+-DCART_PAGED}"; [ "$CART" = ED ] && CART_DEFS="-DCART_PAGED -DCART_ED"\necho "==> Sub-CPU";', 1)
    s = s.rstrip("\n") + '''

# EDCD=1 (MODE1-1, CD32X BootCamp docs/12): EDCD.32X, made by the BootCamp's edboot/build_edcd.sh around
# this disc's Sub-CPU program built a second time for the cartridge boot (MODE1=1 M1PAGE=1, in a copy
# of cd32x/subcpu: the disc keeps the normal program)
if [ "${EDCD:-0}" = 1 ]; then
  BC="${BOOTCAMP:-$HERE/../CD32X-BootCamp}"
  [ -f "$BC/edboot/build_edcd.sh" ] || { echo "*** EDCD=1 needs the CD32X BootCamp next to this folder (or BOOTCAMP=<folder>): not at $BC"; exit 1; }
  echo "==> EDCD.32X: the disc started from the EverDrive PRO (cartridge boot)"
  T=$(mktemp -d); mkdir -p "$T/subcpu/ROMS" "$T/sh2"
  find "$HERE/cd32x/subcpu" -maxdepth 1 -type f -exec cp {} "$T/subcpu/" \\;
  cp "$HERE/cd32x/sh2/CDLOADER.32x" "$T/sh2/"
  ( cd "$T/subcpu" && make clean > /dev/null && make CDROMPlayer.bin MODE1=1 M1CTRL0=0x8000 M1PAGE=1 CART_DEFS="$CART_DEFS" > "$T/mode1.log" 2>&1 ) \\
    || { tail -20 "$T/mode1.log"; echo "*** the MODE1 Sub-CPU program did not build"; exit 1; }
  bash "$BC/edboot/build_edcd.sh" "$T/subcpu/CDROMPlayer.bin" "$HERE/cd32x/sh2/CDLOADER.32x" "$HERE/cd32x/subcpu/EDCD.32X"
  rm -rf "$T"
  echo "==> EDCD.32X: $HERE/cd32x/subcpu/EDCD.32X - copy it to the EverDrive's SD card and start it from the menu"
  echo "    with the disc above in the Sega CD (not \\"Run Mega-CD\\")"
fi
'''
    open(p, "w").write(s); print("  build.sh: ok")
else:
    print("  build.sh: already")
print("done")
