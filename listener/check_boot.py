#!/usr/bin/env python3
"""check_boot.py - is the boot stack still the one that runs on the console?

    python3 tools/check_boot.py            check (build.sh runs it first; exit 1 = changed)
    python3 tools/check_boot.py --update   write stack/BOOT_FILES.md5 again (only when you MEANT to change
                                           the stack, and you have run the new one on a console)

Every source file of stack/subcpu and stack/sh2 is compared with stack/BOOT_FILES.md5.  Two things may
differ without failing the check:
  - stack/subcpu/game_files.h  (your file names: not part of the hash list, but it may hold only
                                #defines and lists of 8.3 file names - it is compiled into main.c)
  - the four lines of the diagnostic disc (tools/diag_switch.py: KOBO_DIAG_TEXT, KOBO_BOOT_TEXT in main.c,
    the display line in hw_md.s, the CMD 52 argument in cdloader_sh2.c): either their release or their
    diagnostic form is accepted (build.sh checks a release disc has the release lines).
The idea is SuperTux CD32X REV's tools/check_boot.py.
"""
import hashlib, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import diag_switch                          # the diagnostic disc's switches: either line is accepted

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LIST = os.path.join(ROOT, "stack", "BOOT_FILES.md5")
DIRS = ["stack/subcpu", "stack/sh2"]
EXTS = (".c", ".h", ".s", ".ld", ".py", ".md")
NAMES = ("Makefile", "Makefile.cdloader")
SKIP = {"stack/subcpu/game_files.h"}


def files():
    out = []
    for d in DIRS:
        for n in sorted(os.listdir(os.path.join(ROOT, d))):
            rel = d + "/" + n
            if rel in SKIP or not os.path.isfile(os.path.join(ROOT, rel)):
                continue
            if n.endswith(EXTS) or n in NAMES:
                out.append(rel)
    return out


def digest(rel):
    data = open(os.path.join(ROOT, rel), "rb").read().replace(b"\r\n", b"\n")
    return hashlib.md5(diag_switch.to_release(rel, data)).hexdigest()


GAME_FILES_OK = re.compile(r'^(#ifndef GAME_FILES_H|#define GAME_FILES_H|#endif|#define GAME_[A-Z_]+\s+("[^"]*"|\d+)'
                           r'|static const char \*const game_(data|movie)_files\[\] = \{|"[A-Z0-9_]{1,8}\.[A-Z0-9_]{1,3}",?|\};)$')


def check_game_files():
    """game_files.h is compiled into the Sub-CPU program: it may hold names and numbers, nothing else."""
    bad = []
    text = open(os.path.join(ROOT, "stack/subcpu/game_files.h")).read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)          # comments
    for n, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if line and not GAME_FILES_OK.match(line):
            bad.append(line)
    m = re.search(r'#define GAME_ROM_NAME\s+"([^"]*)"', text)
    if not m or not re.fullmatch(r"[A-Z0-9_]{1,8}\.[A-Z0-9_]{1,3}", m.group(1)):
        bad.append("GAME_ROM_NAME must be an ISO 8.3 name in capitals (main.c copies it into 24 bytes)")
    for name in ("GAME_DATA_SLOT", "GAME_RESIDENT_SLOT"):
        m = re.search(r"#define %s\s+(\d+)" % name, text)
        if not m or not 4 <= int(m.group(1)) <= 31:
            bad.append("%s must be 4-31: slot n = cart n x 128 KB (0-3 is the ROM, 32 and up is not the cart)" % name)
    m = re.search(r'#define GAME_BRAM_NAME\s+"([^"]*)"', text)
    if not m or not re.fullmatch(r"[A-Z0-9_]{11}", m.group(1)):
        bad.append("GAME_BRAM_NAME must be exactly 11 characters A-Z 0-9 _")
    data = re.search(r"game_data_files\[\] = \{(.*?)\};", text, re.S)
    if data and len(re.findall(r'"', data.group(1))) // 2 > 100:
        bad.append("game_data_files: at most 100 names (200 + n would overlap 100 + n)")
    return bad


def main():
    gf = check_game_files()
    if gf:
        print("check_boot: stack/subcpu/game_files.h may hold only #defines and lists of 8.3 file names:")
        for line in gf:
            print("  " + line)
        return 1
    now = {rel: digest(rel) for rel in files()}
    if "--update" in sys.argv:
        with open(LIST, "w") as f:
            for rel in sorted(now):
                f.write("%s  %s\n" % (now[rel], rel))
        print("check_boot: wrote %d files to %s" % (len(now), os.path.relpath(LIST, ROOT)))
        return 0
    want = {}
    for line in open(LIST):
        if line.strip():
            h, rel = line.split(None, 1)
            want[rel.strip()] = h
    bad = []
    for rel in sorted(set(want) | set(now)):
        if rel not in now:
            bad.append("missing  " + rel)
        elif rel not in want:
            bad.append("new      " + rel)
        elif want[rel] != now[rel]:
            bad.append("changed  " + rel)
    if bad:
        print("check_boot: the boot stack is NOT the one this kit ships:")
        for b in bad:
            print("  " + b)
        print("Undo the change (only game_files.h and the diagnostic switches of tools/diag_switch.py may differ).")
        print("See docs/06_HARDWARE_RULES.md before changing the stack on purpose.")
        return 1
    print("check_boot: the boot stack is unchanged (%d files)" % len(now))
    return 0


if __name__ == "__main__":
    sys.exit(main())
