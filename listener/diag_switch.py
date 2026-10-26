#!/usr/bin/env python3
"""diag_switch.py - the diagnostic disc's switches (build.sh DIAG=1 runs "on" before the build, "off" after).

    python3 tools/diag_switch.py on      the diagnostic lines in, the files saved as <file>.diagsave
    python3 tools/diag_switch.py off     the saved files back (the release lines)
    python3 tools/diag_switch.py check   which state each switch is in
    python3 tools/diag_switch.py release exit 1 unless all are the release lines (build.sh, release disc)

Each switch is ONE existing line of the boot stack, swapped for its diagnostic twin.  (SuperTux CD32X REV
keeps the same four diagnostics as #define switches and edited lines, docs/BOOT_PATH_NOTES.md there.)  The
release disc never has them; the source in git always holds the release lines; tools/check_boot.py accepts
either line, and build.sh refuses to build a release disc unless all four are the release lines.
In the hw_md.s twin the old comment "display OFF" stays next to 0x8174: the line is swapped whole.

  1 main.c        KOBO_DIAG_TEXT 1      the Sub-CPU's green text (boot steps, STATE, files, cart chunks)
  2 main.c        KOBO_BOOT_TEXT 1      main() does not switch the Genesis display off
  3 hw_md.s       0x8174 (OL-13)        the 68000 leaves the Genesis display ON: the white 32X start-up
                                        lines and the green text show from the start
  4 cdloader      CMD 52 COMM2 = 1      CDLOADER keeps the Genesis text on at 4D01 (OL-3 sends 0)
"""
import os, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (file, release line, diagnostic line) - each release line must occur exactly once
SWITCHES = [
    ("stack/subcpu/main.c",
     b"#define KOBO_DIAG_TEXT        0",
     b"#define KOBO_DIAG_TEXT        1"),
    ("stack/subcpu/main.c",
     b"#define KOBO_BOOT_TEXT        0",
     b"#define KOBO_BOOT_TEXT        1"),
    ("stack/subcpu/hw_md.s",
     b"        move.w  #0x8134,(a4)            | OL-13: display OFF (V-int on) - no white boot text",
     b"        move.w  #0x8174,(a4)            | OL-13: display OFF (V-int on) - no white boot text"),
    ("stack/sh2/cdloader_sh2.c",
     b"    SC_COMM2 = 0;                        /* OL-3: Genesis text stays off */",
     b"    SC_COMM2 = 1;                        /* OL-3: Genesis text stays off */"),
]


def to_release(rel, data):
    """data with every diagnostic line of this file put back to its release line (for check_boot.py)"""
    for f, release, diag in SWITCHES:
        if f == rel:
            data = data.replace(diag, release)
    return data


def files():
    return sorted(set(f for f, _, _ in SWITCHES))


def on():
    # check everything before touching anything: a half-switched stack must never be left behind
    for rel in files():
        p = os.path.join(ROOT, rel)
        if os.path.exists(p + ".diagsave"):
            sys.exit("diag_switch: %s.diagsave exists (an interrupted build?) - run: python3 tools/diag_switch.py off"
                     % rel)
    for f, release, diag in SWITCHES:
        if open(os.path.join(ROOT, f), "rb").read().count(release) != 1:
            sys.exit("diag_switch: the release line is not in %s exactly once:\n  %s" % (f, release.decode()))
    for rel in files():
        p = os.path.join(ROOT, rel)
        data = open(p, "rb").read()
        shutil.copy2(p, p + ".diagsave")    # copy2: the file's mode and time are kept
        for f, release, diag in SWITCHES:
            if f == rel:
                data = data.replace(release, diag)
        with open(p, "wb") as out:          # rewritten in place: the mode stays
            out.write(data)
    print("diag_switch: %d diagnostic switches on" % len(SWITCHES))


def off():
    n = 0
    for rel in files():
        p = os.path.join(ROOT, rel)
        if os.path.exists(p + ".diagsave"):
            os.replace(p + ".diagsave", p)  # the saved copy, with its original mode and time
            n += 1
    print("diag_switch: release lines back in %d files" % n)


def release():
    """exit 1 unless every switch holds its release line (build.sh runs this for a release disc)"""
    bad = [f for f, rel_line, _ in SWITCHES if open(os.path.join(ROOT, f), "rb").read().count(rel_line) != 1]
    left = [f for f in files() if os.path.exists(os.path.join(ROOT, f) + ".diagsave")]
    if bad or left:
        print("diag_switch: NOT the release lines (%s)%s - run: python3 tools/diag_switch.py off"
              " (with no .diagsave: git checkout the file)"
              % (", ".join(sorted(set(bad))) or "-", "; .diagsave left: " + ", ".join(left) if left else ""))
        sys.exit(1)
    print("diag_switch: release lines")


def check():
    for f, release, diag in SWITCHES:
        data = open(os.path.join(ROOT, f), "rb").read()
        state = "release" if data.count(release) == 1 else "DIAGNOSTIC" if data.count(diag) == 1 else "MISSING"
        print("%-10s %s: %s" % (state, f, release.decode().strip()[:60]))


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    {"on": on, "off": off, "check": check, "release": release}.get(cmd, lambda: sys.exit(__doc__))()
