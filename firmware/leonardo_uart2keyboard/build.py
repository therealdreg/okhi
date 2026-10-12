# MIT License - okhi - Open Keylogger Hardware Implant
# ---------------------------------------------------------------------------
# Copyright (c) [2024] by David Reguera Garcia aka Dreg
# https://github.com/therealdreg/okhi
# https://www.rootkit.es
# X @therealdreg
# dreg@rootkit.es
# ---------------------------------------------------------------------------
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
# ---------------------------------------------------------------------------
# WARNING: BULLSHIT CODE X-)
# ---------------------------------------------------------------------------

import glob
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = ".exe" if os.name == "nt" else ""


def arduino15_dirs():
    home = os.path.expanduser("~")
    if os.name == "nt":
        roots = [os.path.join(os.environ.get("LOCALAPPDATA", ""), "Arduino15")]
    elif sys.platform == "darwin":
        roots = [os.path.join(home, "Library", "Arduino15")]
    else:
        roots = [os.path.join(home, ".arduino15")]
    roots.append(os.path.join(home, "Arduino15"))
    return [os.path.join(r, "packages", "arduino") for r in roots if r]


def newest(pattern):
    hits = sorted(glob.glob(pattern))
    return hits[-1] if hits else None


def find_tool(env, name, subdir, relative):
    """Env override, then the newest version installed by the Arduino AVR package, then PATH."""
    override = os.environ.get(env)
    if override:
        return override
    for a15 in arduino15_dirs():
        hit = newest(os.path.join(a15, "tools", subdir, "*", relative))
        if hit:
            return hit
    return shutil.which(name)


GCC = find_tool("OKHI_AVR_GCC", "avr-gcc", "avr-gcc", os.path.join("bin", "avr-gcc" + EXE))
GCC_DIR = os.path.dirname(GCC) if GCC else ""
OBJCOPY = os.path.join(GCC_DIR, "avr-objcopy" + EXE) if GCC_DIR else shutil.which("avr-objcopy")
SIZE = os.path.join(GCC_DIR, "avr-size" + EXE) if GCC_DIR else shutil.which("avr-size")

# A standalone avrdude (AVRDUDESS and friends) keeps its conf next to the binary; the Arduino
# package keeps it in ../etc. Try both, in that order, around whatever binary we resolved.
AVRDUDESS = r"C:\Users\regue\Downloads\AVRDUDESS-2.20-portable (1)"
AVRDUDE = os.environ.get("OKHI_AVRDUDE")
if not AVRDUDE and os.path.exists(os.path.join(AVRDUDESS, "avrdude" + EXE)):
    AVRDUDE = os.path.join(AVRDUDESS, "avrdude" + EXE)
if not AVRDUDE:
    AVRDUDE = find_tool("OKHI_AVRDUDE", "avrdude", "avrdude", os.path.join("bin", "avrdude" + EXE))

AVRCONF = os.environ.get("OKHI_AVRDUDE_CONF")
if not AVRCONF and AVRDUDE:
    here = os.path.dirname(AVRDUDE)
    for cand in (os.path.join(here, "avrdude.conf"),
                 os.path.join(os.path.dirname(here), "etc", "avrdude.conf")):
        if os.path.exists(cand):
            AVRCONF = cand
            break

SRC = os.path.join(HERE, "okhi_kbd.c")
OUT = os.path.join(HERE, "build")
ELF = os.path.join(OUT, "okhi_kbd.elf")
HEX = os.path.join(OUT, "okhi_kbd.hex")

CFLAGS = [
    "-mmcu=atmega32u4",
    "-DF_CPU=16000000UL",
    "-DUSB_LOW_SPEED=1",
    "-DFORCE_SPEED=1",
    "-Os",
    "-std=gnu11",
    "-Wall",
    "-Wextra",
    "-ffunction-sections",
    "-fdata-sections",
    "-fno-common",
    "-funsigned-char",
    "-funsigned-bitfields",
    "-fpack-struct",
    "-fshort-enums",
]
LDFLAGS = ["-mmcu=atmega32u4", "-Wl,--gc-sections", "-Wl,--relax"]

EXPECTED_FUSES = (("lfuse", 0xFF), ("hfuse", 0xD9), ("efuse", 0xCB))


def run(cmd, **kw):
    print("+", " ".join(os.path.basename(c) if i == 0 else c for i, c in enumerate(cmd)))
    return subprocess.run(cmd, **kw)


def hex_bytes(path):
    mem = {}
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        n = int(line[1:3], 16)
        rtype = int(line[7:9], 16)
        if rtype != 0:
            continue
        addr = int(line[3:7], 16)
        for i in range(n):
            mem[addr + i] = int(line[9 + 2 * i:11 + 2 * i], 16)
    return mem


def verify_chip(expected_path):
    readback = os.path.join(OUT, "readback.hex")
    r = run([AVRDUDE, "-C", AVRCONF, "-c", "usbasp", "-p", "m32u4",
             "-U", "flash:r:" + readback + ":i"])
    if r.returncode != 0:
        return r.returncode

    want = hex_bytes(expected_path)
    got = hex_bytes(readback)
    bad = [a for a, v in sorted(want.items()) if got.get(a, 0xFF) != v]
    if bad:
        print("MISMATCH: %d of %d bytes differ, first at 0x%04X (want 0x%02X got 0x%02X)"
              % (len(bad), len(want), bad[0], want[bad[0]], got.get(bad[0], 0xFF)))
        return 1
    print("readback verified: %d bytes match" % len(want))
    return 0


def read_fuse(name):
    r = subprocess.run([AVRDUDE, "-C", AVRCONF, "-c", "usbasp", "-p", "m32u4",
                        "-U", name + ":r:-:h"], capture_output=True, text=True)
    m = re.search(r"0x([0-9a-fA-F]{2})", r.stdout) if r.returncode == 0 else None
    if m is None:
        for line in (r.stderr or "").strip().splitlines()[-4:]:
            print("    " + line)
        return None
    return int(m.group(1), 16)


def fuse_meaning(name, got, want):
    if name != "hfuse":
        return []
    diff = got ^ want
    out = []
    if diff & 0x01:
        if got & 0x01:
            out.append("BOOTRST unprogrammed: reset goes straight to 0x0000")
        else:
            out.append("BOOTRST programmed: reset jumps to the boot section at 0x7000")
            out.append("instead of the application. this project flashes a bare avr")
            out.append("image and the chip erase leaves that section blank, so there is")
            out.append("nothing valid to jump to. on a board that still carries the")
            out.append("factory arduino bootloader it runs that instead, which holds the")
            out.append("usb bus about 8 s and never reads the uart, so commands sent in")
            out.append("that window are lost and the board just repeats READY every 3 s")
    if diff & 0x20:
        out.append("SPIEN differs: with SPIEN unprogrammed isp is dead and only an hv")
        out.append("programmer can recover the chip")
    if diff & 0x10:
        out.append("WDTON differs: the watchdog would be forced always on")
    if diff & 0x08:
        out.append("EESAVE differs: changes whether a chip erase keeps the eeprom")
    if diff & 0x06:
        out.append("BOOTSZ differs: boot section is no longer 4096 bytes at 0x7000")
    return out


def fuse_report(bad):
    bar = "!" * 74
    print(bar)
    print("!! FUSE MISMATCH, the board is not set up the way this project expects")
    for name, got, want in bad:
        print("!!   %-5s is 0x%02X, expected 0x%02X" % (name, got, want))
        for line in fuse_meaning(name, got, want):
            print("!!     " + line)
    print("!!")
    print("!! fix by re-running with --fix-fuses, or by hand:")
    for name, got, want in bad:
        print('!!   avrdude -C "%s" -c usbasp -p m32u4 -U %s:w:0x%02X:m'
              % (AVRCONF, name, want))
    print(bar)


def fuse_check(do_fix):
    print("=== fuse check ===")
    bad = []
    unread = 0
    for name, want in EXPECTED_FUSES:
        got = read_fuse(name)
        if got is None:
            print("  %-5s could not be read, is the usbasp connected?" % name)
            unread += 1
        elif got == want:
            print("  %-5s 0x%02X ok" % (name, got))
        else:
            print("  %-5s 0x%02X WRONG, expected 0x%02X" % (name, got, want))
            bad.append((name, got, want))

    if bad and do_fix:
        print("=== writing correct fuses ===")
        still = []
        for name, got, want in bad:
            r = run([AVRDUDE, "-C", AVRCONF, "-c", "usbasp", "-p", "m32u4",
                     "-U", "%s:w:0x%02X:m" % (name, want)])
            now = read_fuse(name) if r.returncode == 0 else None
            if now == want:
                print("  %-5s now 0x%02X ok" % (name, now))
            else:
                print("  %-5s FAILED to set to 0x%02X" % (name, want))
                still.append((name, got if now is None else now, want))
        bad = still

    if bad:
        fuse_report(bad)
    elif unread == 0:
        print("  all fuses as expected")
    return bad


USAGE = """usage: build.py [--flash] [--fix-fuses] [-D<name>[=<value>] ...]

  (no argument)  compile only, never calls avrdude
  --flash        compile, check fuses, flash over USBasp, read the chip back and verify
  --fix-fuses    check the fuses and write the correct ones, no flashing
  -D<name>       extra define passed straight to gcc, e.g. -DBOARD_LEONARDO=1

tool paths are found in this order: the environment variable, then the newest version
installed by the Arduino AVR package, then PATH.

  OKHI_AVR_GCC        full path to avr-gcc (avr-objcopy and avr-size are taken beside it)
  OKHI_AVRDUDE        full path to avrdude
  OKHI_AVRDUDE_CONF   full path to avrdude.conf"""


def tools_report():
    print("avr-gcc  :", GCC or "NOT FOUND")
    print("avrdude  :", AVRDUDE or "NOT FOUND")
    print("conf     :", AVRCONF or "NOT FOUND")


def main():
    argv = sys.argv[1:]
    if "--help" in argv or "-h" in argv:
        print(USAGE)
        print()
        tools_report()
        return 0

    extra = [a for a in argv if a.startswith("-D")]
    do_flash = "--flash" in argv
    do_fix = "--fix-fuses" in argv

    known = set(extra) | {"--flash", "--fix-fuses"}
    unknown = [a for a in argv if a not in known]
    if unknown:
        print("unknown argument: " + " ".join(unknown))
        print()
        print(USAGE)
        return 2

    if not GCC or not os.path.exists(GCC):
        print("avr-gcc not found. Install the Arduino AVR board package or set OKHI_AVR_GCC.")
        print()
        tools_report()
        return 2
    if (do_flash or do_fix) and not (AVRDUDE and AVRCONF):
        print("avrdude or avrdude.conf not found. Set OKHI_AVRDUDE and OKHI_AVRDUDE_CONF.")
        print()
        tools_report()
        return 2

    os.makedirs(OUT, exist_ok=True)
    for f in (ELF, HEX):
        if os.path.exists(f):
            os.remove(f)

    r = run([GCC] + CFLAGS + extra + LDFLAGS + [SRC, "-o", ELF])
    if r.returncode != 0:
        return r.returncode

    r = run([OBJCOPY, "-O", "ihex", "-R", ".eeprom", ELF, HEX])
    if r.returncode != 0:
        return r.returncode

    out = subprocess.run([SIZE, "-C", "--mcu=atmega32u4", ELF], capture_output=True, text=True)
    print(out.stdout.strip())

    bad = fuse_check(do_fix) if (do_flash or do_fix) else []

    if do_flash:
        r = run([AVRDUDE, "-C", AVRCONF, "-c", "usbasp", "-p", "m32u4",
                 "-U", "flash:w:" + HEX + ":i"])
        if r.returncode != 0:
            return r.returncode
        rc = verify_chip(HEX)
        if bad:
            print("")
            fuse_report(bad)
        return rc
    return 0


if __name__ == "__main__":
    sys.exit(main())
