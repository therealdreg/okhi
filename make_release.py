#!/usr/bin/env python3
"""
okhi release builder - by Dreg

Builds every firmware target and packages a release. Replaces make_release.ps1,
which only packaged and only ran on Windows. Pure standard library, no pip
installs, and the same behaviour on Windows, Linux and macOS.

    python make_release.py                 build everything, then package
    python make_release.py --no-build      package what is already built
    python make_release.py --build-only    build everything, do not package
    python make_release.py --targets usb/esp usb/rp
    python make_release.py --list          show the toolchains it found and exit

Output goes to release/<timestamp>/ under the repo root. That whole directory is
gitignored, so a release can never be committed by accident: the ESP images it
copies carry whatever was in wifi_secret.h when they were built.

Toolchains are auto-detected and every path can be overridden with an environment
variable, so a machine that keeps them somewhere unusual needs no edits:

    OKHI_IDF_PATH          ESP-IDF checkout               (default: auto)
    OKHI_IDF_PYTHON        python of the IDF virtualenv   (default: auto)
    OKHI_IDF_TOOLS_PATH    IDF_TOOLS_PATH                 (default: auto)
    OKHI_IDF_PROFILE       Windows PowerShell activation profile (default: auto)
    OKHI_CMAKE             cmake binary                   (default: auto, then PATH)
    OKHI_NINJA             ninja binary                   (default: auto, then PATH)
"""

import argparse
import glob
import gzip
import hashlib
import json
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parent
IS_WINDOWS = platform.system() == "Windows"

VARIANTS = ("usb", "ps2")
ALL_TARGETS = ("usb/esp", "usb/rp", "ps2/esp", "ps2/rp", "uart_bridge")

errors = []
warnings = []


# ---------------------------------------------------------------- reporting

def ok(msg):
    print("[OK] %s" % msg)


def fail(msg):
    errors.append(msg)
    print("[FAIL] %s" % msg)


def warn(msg):
    warnings.append(msg)
    print("[WARN] %s" % msg)


def step(msg):
    print("\n=== %s ===" % msg)


# ---------------------------------------------------------------- crc32
# The device checks the package with the standard CRC-32 (the zlib one). The
# known answer test below runs before anything is written, because a mismatch
# here only shows up once the package is already on the implant.

def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def assert_crc32():
    value = crc32(b"123456789")
    if value != 0xCBF43926:
        raise SystemExit("crc32 self test failed: got %08x, expected cbf43926" % value)
    ok("crc32 self test")


# ---------------------------------------------------------------- toolchains

def find_idf():
    """Locate an ESP-IDF install. Returns a dict or None."""
    env_path = os.environ.get("OKHI_IDF_PATH") or os.environ.get("IDF_PATH")
    env_python = os.environ.get("OKHI_IDF_PYTHON")
    env_tools = os.environ.get("OKHI_IDF_TOOLS_PATH") or os.environ.get("IDF_TOOLS_PATH")
    env_profile = os.environ.get("OKHI_IDF_PROFILE")

    idf = {"path": env_path, "python": env_python, "tools": env_tools, "profile": env_profile}

    # Windows EIM installs describe themselves in a json file, which is the most
    # reliable source when several IDF versions are present. Consulted even when
    # IDF_PATH is already set, because the interesting parts are the virtualenv
    # python and the activation profile, and an inherited IDF_PATH on its own says
    # nothing about either.
    if IS_WINDOWS:
        for candidate in (Path("C:/Espressif/tools/eim_idf.json"),
                          Path.home() / ".espressif" / "eim_idf.json"):
            if not candidate.is_file():
                continue
            try:
                data = json.loads(candidate.read_text(encoding="utf8"))
            except Exception:
                continue
            installs = data.get("idfInstalled") or []
            selected = data.get("idfSelectedId")
            chosen = next((i for i in installs if i.get("id") == selected), None) or (installs[0] if installs else None)
            if chosen:
                idf["path"] = idf["path"] or chosen.get("path")
                idf["python"] = idf["python"] or chosen.get("python")
                idf["tools"] = idf["tools"] or chosen.get("idfToolsPath")
                idf["profile"] = idf["profile"] or chosen.get("activationScript")
            break

    # Classic layouts.
    if not idf["path"]:
        for candidate in (Path.home() / "esp" / "esp-idf", Path("/opt/esp/idf")):
            if (candidate / "tools" / "idf.py").is_file():
                idf["path"] = str(candidate)
                break

    if not idf["path"] or not (Path(idf["path"]) / "tools" / "idf.py").is_file():
        return None

    return idf


def find_pico_tool(name, env_var):
    """cmake / ninja: explicit override, then the Pico VS Code extension layout, then PATH."""
    override = os.environ.get(env_var)
    if override:
        return override

    exe = name + (".exe" if IS_WINDOWS else "")
    roots = [Path.home() / ".pico-sdk" / name]
    for root in roots:
        if root.is_dir():
            # Newest version directory wins.
            hits = sorted(glob.glob(str(root / "*" / "**" / exe), recursive=True), reverse=True)
            if hits:
                return hits[0]

    found = shutil.which(name)
    return found


def describe_toolchains(idf, cmake, ninja):
    step("toolchains")
    if idf:
        ok("ESP-IDF        %s" % idf["path"])
        if idf.get("python"):
            print("     python     %s" % idf["python"])
        if IS_WINDOWS and idf.get("profile"):
            print("     profile    %s" % idf["profile"])
    else:
        warn("ESP-IDF not found, ESP targets cannot be built (set OKHI_IDF_PATH)")
    print("     cmake      %s" % (cmake or "NOT FOUND"))
    print("     ninja      %s" % (ninja or "NOT FOUND"))


# ---------------------------------------------------------------- cleaning

def rm(path, what):
    """Delete a file or a directory tree, reporting only if it was there."""
    path = Path(path)
    if path.is_dir():
        shutil.rmtree(path, ignore_errors=True)
        print("     removed %s" % what)
        return True
    if path.is_file():
        try:
            path.unlink()
            print("     removed %s" % what)
            return True
        except OSError:
            pass
    return False


def clean(targets, deep):
    """
    Normal clean drops every build/ tree, which is enough for almost everything.

    Deep clean also drops the ESP sdkconfig, dependencies.lock and
    managed_components/. That matters because idf.py fullclean empties build/ but
    KEEPS sdkconfig, and a stale sdkconfig is exactly what has caused breakage
    here before: it is regenerated from sdkconfig.defaults, which is the real
    source of truth and stays put. sdkconfig.defaults, sdkconfig.ci* and
    partitions.csv are never touched.

    A deep clean means both ESP targets compile from scratch, several minutes each.
    """
    step("deep clean" if deep else "clean")

    for t in targets:
        project = REPO / "firmware" / t.replace("/", os.sep)
        rm(project / "build", "%s/build" % t)

        if deep and t.endswith("/esp"):
            rm(project / "sdkconfig", "%s/sdkconfig" % t)
            rm(project / "sdkconfig.old", "%s/sdkconfig.old" % t)
            rm(project / "dependencies.lock", "%s/dependencies.lock" % t)
            rm(project / "managed_components", "%s/managed_components" % t)

    ok("clean done, %d target(s)" % len(targets))


# ---------------------------------------------------------------- building

def run(cmd, cwd=None, env=None, shell=False):
    """Run a command, streaming nothing, returning (rc, output)."""
    try:
        p = subprocess.run(cmd, cwd=cwd, env=env, shell=shell,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return p.returncode, p.stdout.decode("utf8", "replace")
    except FileNotFoundError as e:
        return 127, str(e)


def build_esp(variant, idf, no_ccache):
    """
    Build one ESP target.

    On Windows idf.py has to run under PowerShell with the EIM activation
    profile dot-sourced: it refuses to run under MSys/Git Bash, and the profile
    is what puts the toolchain, ninja and the IDF python on PATH. On Unix the
    equivalent is sourcing export.sh in a shell. Either way the actual build
    command is the same idf.py invocation.
    """
    project = REPO / "firmware" / variant / "esp"
    if not (project / "CMakeLists.txt").is_file():
        fail("no ESP project at %s" % project)
        return False

    if not idf:
        fail("cannot build %s/esp: no ESP-IDF found" % variant)
        return False

    print("  building %s/esp ..." % variant)
    ccache = '0' if no_ccache else '1'

    if IS_WINDOWS:
        profile = idf.get("profile")
        if not profile or not Path(profile).is_file():
            fail("cannot build %s/esp: no PowerShell activation profile (set OKHI_IDF_PROFILE)" % variant)
            return False
        ps = (
            '$env:IDF_CCACHE_ENABLE = "%s"; '
            '. "%s" | Out-Null; '
            '$env:IDF_CCACHE_ENABLE = "%s"; '
            'Invoke-idfpy -C "%s" build'
        ) % (ccache, profile, ccache, project)
        rc, out = run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps])
    else:
        export = Path(idf["path"]) / "export.sh"
        if not export.is_file():
            fail("cannot build %s/esp: %s not found" % (variant, export))
            return False
        sh = 'export IDF_CCACHE_ENABLE=%s; . "%s" >/dev/null && idf.py -C "%s" build' % (ccache, export, project)
        rc, out = run(["bash", "-lc", sh])

    warnings_seen = out.count("warning:")
    if rc != 0:
        for line in out.splitlines():
            if "error:" in line.lower():
                print("     %s" % line.strip()[:160])
        fail("%s/esp build failed (rc %d)" % (variant, rc))
        return False

    ok("%s/esp built, %d compiler warning(s)" % (variant, warnings_seen))
    return True


def build_rp(target, cmake, ninja):
    """Configure (once) and build one Pico SDK project."""
    project = REPO / "firmware" / target.replace("/", os.sep)
    if not (project / "CMakeLists.txt").is_file():
        fail("no Pico project at %s" % project)
        return False
    if not cmake or not ninja:
        fail("cannot build %s: cmake or ninja not found" % target)
        return False

    build = project / "build"
    print("  building %s ..." % target)

    if not (build / "build.ninja").is_file():
        rc, out = run([cmake, "-G", "Ninja",
                       "-S", str(project), "-B", str(build),
                       "-DCMAKE_BUILD_TYPE=Debug",
                       "-DCMAKE_MAKE_PROGRAM=%s" % ninja])
        if rc != 0:
            for line in out.splitlines()[-12:]:
                print("     %s" % line.strip()[:160])
            fail("%s configure failed" % target)
            return False

    rc, out = run([ninja, "-C", str(build)])
    if rc != 0:
        for line in out.splitlines():
            if "error:" in line.lower():
                print("     %s" % line.strip()[:160])
        fail("%s build failed" % target)
        return False

    flash = next((l.strip() for l in out.splitlines() if "FLASH:" in l), "")
    if not flash and "no work to do" in out:
        flash = "already up to date"
    ok("%s built  %s" % (target, flash))

    # The Pico VS Code extension asks cmake for its file API through this query
    # file. A command line configure never creates it, and wiping build/ loses
    # it, which quietly breaks IntelliSense and the extension's target list.
    qdir = build / ".cmake" / "api" / "v1" / "query" / "client-vscode"
    qdir.mkdir(parents=True, exist_ok=True)
    (qdir / "query.json").write_text(
        '{"requests":[{"kind":"cache","version":2},{"kind":"codemodel","version":2},'
        '{"kind":"toolchains","version":1},{"kind":"cmakeFiles","version":1}]}',
        encoding="utf8")
    return True


def run_webgen(variant):
    """
    Regenerate the embedded web page for one ESP variant, before that ESP is
    built. The page is linked into the application, so it only reaches the device
    when the ESP is rebuilt: webgen has to run first or the release ships the old
    page while nothing warns. webgen.js also lints the page and exits non-zero on a
    parse error, so a failure here must stop the build rather than embed a stale gz.
    """
    folder = REPO / ("webusb" if variant == "usb" else "webps2")
    script = folder / "webgen.js"
    if not script.is_file():
        fail("no webgen.js for %s at %s" % (variant, script))
        return False

    node = shutil.which("node")
    if not node:
        fail("cannot regenerate the %s web page: node was not found on PATH" % variant)
        return False

    print("  webgen %s ..." % variant)
    rc, out = run([node, "webgen.js"], cwd=str(folder))
    if rc != 0:
        for line in out.splitlines():
            print("     %s" % line.strip()[:160])
        fail("webgen.js failed for %s (rc %d), the ESP was NOT built with a fresh page" % (variant, rc))
        return False

    ok("%s web page regenerated" % variant)
    return True


def build_all(targets, idf, cmake, ninja, no_ccache):
    step("build")
    good = True
    for t in targets:
        if t.endswith("/esp"):
            variant = t.split("/")[0]
            if run_webgen(variant):
                good &= build_esp(variant, idf, no_ccache)
            else:
                good = False
        else:
            good &= build_rp(t, cmake, ninja)
    return good


# ---------------------------------------------------------------- packaging

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def copy_verified(src, dst, label=None):
    name = label or Path(src).name
    if not Path(src).is_file():
        fail("MISSING %s" % src)
        return False
    Path(dst).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    if not Path(dst).is_file():
        fail("COPY DID NOT LAND %s -> %s" % (src, dst))
        return False
    if sha256(src) != sha256(dst):
        fail("VERIFY FAILED %s != %s" % (src, dst))
        return False
    ok("%s  ->  %s" % (name, Path(dst).name))
    return True


def check_embedded_page(variant, esp_bytes, ignore_stale):
    """
    The page is linked into the ESP application, so only a rebuild moves it. If
    the gz in main/ is not inside okhi.bin the release carries a page nobody will
    ever see and the implant keeps serving the previous one.
    """
    page_path = REPO / "firmware" / variant / "esp" / "main" / "index.html.gz"
    if not page_path.is_file():
        warn("%s has no main/index.html.gz, cannot check the embedded page" % variant)
        return
    page = page_path.read_bytes()
    if page in esp_bytes:
        ok("%s esp carries the current web page (%d bytes)" % (variant, len(page)))
        return
    msg = ("%s esp does NOT carry the current main/index.html.gz. "
           "Run webgen.js then rebuild the ESP, or the implant serves the old page." % variant)
    warn(msg) if ignore_stale else fail(msg)


def make_package(esp_image, rp_image, variant, output, ignore_stale):
    esp = Path(esp_image).read_bytes()
    rp = Path(rp_image).read_bytes()

    if len(esp) < 8192:
        raise ValueError("ESP image looks too small: %d bytes" % len(esp))
    if esp[0] != 0xE9:
        raise ValueError("ESP image does not start with the 0xE9 image magic")
    if len(rp) < 1024:
        raise ValueError("RP image looks too small: %d bytes" % len(rp))

    check_embedded_page(variant.lower(), esp, ignore_stale)

    esp_crc = crc32(esp)
    rp_crc = crc32(rp)
    tag = variant.encode("ascii").ljust(4, b"\0")

    header = bytearray(32)
    header[0:4] = b"OKHI"
    struct.pack_into("<I", header, 4, 1)
    header[8:12] = tag
    struct.pack_into("<I", header, 12, len(esp))
    struct.pack_into("<I", header, 16, esp_crc)
    struct.pack_into("<I", header, 20, len(rp))
    struct.pack_into("<I", header, 24, rp_crc)
    struct.pack_into("<I", header, 28, crc32(bytes(header[0:28])))

    Path(output).parent.mkdir(parents=True, exist_ok=True)
    with open(output, "wb") as f:
        f.write(header)
        f.write(esp)
        f.write(rp)

    # Read it back and take it apart the way the device will, so a package that
    # cannot be parsed never leaves this machine.
    check = Path(output).read_bytes()
    total = 32 + len(esp) + len(rp)
    if len(check) != total:
        raise ValueError("package is %d bytes, expected %d" % (len(check), total))
    if check[0:4] != b"OKHI":
        raise ValueError("package does not start with the OKHI magic")
    if struct.unpack_from("<I", check, 12)[0] != len(esp) or struct.unpack_from("<I", check, 16)[0] != esp_crc:
        raise ValueError("esp length or crc in the header does not match the payload")
    if struct.unpack_from("<I", check, 20)[0] != len(rp) or struct.unpack_from("<I", check, 24)[0] != rp_crc:
        raise ValueError("rp length or crc in the header does not match the payload")
    if crc32(check[32:32 + len(esp)]) != esp_crc:
        raise ValueError("esp payload does not match its own crc after write back")
    if crc32(check[32 + len(esp):32 + len(esp) + len(rp)]) != rp_crc:
        raise ValueError("rp payload does not match its own crc after write back")

    print("[OK] %s  variant %s  esp %d bytes crc %08x  rp %d bytes crc %08x  total %d bytes"
          % (Path(output).name, variant, len(esp), esp_crc, len(rp), rp_crc, total))


def fnv1a64(s):
    """FNV-1a 64-bit over the UTF-8 bytes. Byte for byte identical to the releaseFingerprint()
    in webusb/webps2 index.html, so the fingerprint the page shows and the one written here match."""
    h = 0xCBF29CE484222325
    for b in s.encode("utf-8"):
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def variant_identities(variant):
    """
    The three per-compilation identities the web page shows at the top, read from the same built
    artifacts a release ships: esp_image (app sha), rp_identity (the RP_IDENTITY literal) and the
    WEBGEN_STAMP baked into the embedded page. Returns a dict; any field that cannot be read is None.
    """
    esp_bin = REPO / ("firmware/%s/esp/build/okhi.bin" % variant)
    rp_bin = REPO / ("firmware/%s/rp/build/okhi.bin" % variant)
    gz = REPO / ("firmware/%s/esp/main/index.html.gz" % variant)

    esp_hex = None
    if esp_bin.is_file():
        esp_hex = esp_bin.read_bytes()[0xB0:0xB8].hex()

    rp_id = None
    if rp_bin.is_file():
        m = re.search(rb"v[0-9]+ " + variant.encode() +
                      rb" [A-Za-z]{3} [ 0-9][0-9] 20[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}",
                      rp_bin.read_bytes())
        rp_id = m.group(0).decode() if m else None

    web_stamp = None
    if gz.is_file():
        page = gzip.decompress(gz.read_bytes()).decode("utf-8", "replace")
        m = re.search(r"const WEBGEN_STAMP = '([^']*)'", page)
        if m:
            web_stamp = m.group(1)

    fp = None
    if esp_hex and rp_id and web_stamp:
        fp = fnv1a64("%s|%s|%s" % (esp_hex, rp_id, web_stamp))

    return {"esp": esp_hex, "rp": rp_id, "web": web_stamp, "fp": fp}


def write_manifest(out_dir):
    """
    Write the expected identities so that after flashing, opening the web page and reading the
    ESP/RP/WEB/FP lines at the top tells you at a glance whether every part is this release. The
    FP line on the page equals the fingerprint here only when all three parts match.
    """
    step("manifest")
    lines = [
        "okhi release manifest",
        "generated by make_release.py",
        "",
        "After flashing, open http://<board-ip>/ and read the ESP / RP / WEB / FP lines at the top.",
        "They must equal the values below for the matching variant. FP is one value over all three,",
        "so a single match confirms the whole combination is this release.",
        "",
    ]
    for variant in ("ps2", "usb"):
        ids = variant_identities(variant)
        lines.append("[%s]" % variant.upper())
        lines.append("  ESP (esp_image) : %s" % (ids["esp"] or "MISSING (build the esp target)"))
        lines.append("  RP  (rp_identity): %s" % (ids["rp"] or "MISSING (build the rp target)"))
        lines.append("  WEB (webgen)    : %s" % (ids["web"] or "MISSING (run webgen.js and rebuild the esp)"))
        lines.append("  FP  (fingerprint): %s" % (ids["fp"] or "n/a (one of the three above is missing)"))
        lines.append("")

    text = "\n".join(lines)
    path = Path(out_dir) / "MANIFEST.txt"
    path.write_text(text, encoding="utf-8")
    ok("MANIFEST.txt written")
    for line in text.splitlines():
        if line.strip():
            print("     %s" % line)


def stage(variant, out_dir, ignore_stale):
    step(variant.upper())
    dest = Path(out_dir) / variant
    dest.mkdir(parents=True, exist_ok=True)

    items = [
        "firmware/%s/esp/esptool.exe" % variant,
        "firmware/%s/esp/upload_firmware.bat" % variant,
        "firmware/%s/esp/build/okhi.bin" % variant,
        "firmware/%s/esp/build/bootloader/bootloader.bin" % variant,
        "firmware/%s/esp/build/partition_table/partition-table.bin" % variant,
        "firmware/%s/esp/build/storage.bin" % variant,
        "firmware/%s/rp/build/okhi.uf2" % variant,
        "stuff/okhi_reset_flash.uf2",
        "firmware/uart_bridge/build/uart_bridge.uf2",
    ]
    for item in items:
        copy_verified(REPO / item, dest / Path(item).name, item)

    # Renamed so both chips can sit side by side in the same folder.
    copy_verified(REPO / ("firmware/%s/esp/build/okhi.bin" % variant), dest / "ota_esp.bin", "esp ota payload")
    copy_verified(REPO / ("firmware/%s/rp/build/okhi.bin" % variant), dest / "ota_rp.bin", "rp ota payload")

    esp_image = REPO / ("firmware/%s/esp/build/okhi.bin" % variant)
    rp_image = REPO / ("firmware/%s/rp/build/okhi.bin" % variant)
    tag = variant.upper()
    if esp_image.is_file() and rp_image.is_file():
        try:
            make_package(esp_image, rp_image, tag, dest / ("okhi_%s_ota.pkg" % tag), ignore_stale)
        except Exception as e:
            fail("package %s: %s" % (tag, e))
    else:
        fail("package %s skipped, an input image is missing" % tag)


def package_all(out_dir, ignore_stale):
    for variant in ("ps2", "usb"):
        stage(variant, out_dir, ignore_stale)

    step("shared")
    src = REPO / "stuff" / "last_esptool"
    if not src.is_dir():
        fail("MISSING %s" % src)
        return
    dst = Path(out_dir) / "last_esptool"
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    n_src = sum(1 for _ in src.rglob("*") if _.is_file())
    n_dst = sum(1 for _ in dst.rglob("*") if _.is_file())
    if n_src != n_dst:
        fail("last_esptool copied %d files, expected %d" % (n_dst, n_src))
    else:
        ok("last_esptool (%d files)" % n_src)

    write_manifest(out_dir)


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description="Build every okhi target and package a release.")
    ap.add_argument("--no-build", action="store_true", help="package what is already built")
    ap.add_argument("--build-only", action="store_true", help="build, do not package")
    ap.add_argument("--targets", nargs="+", default=list(ALL_TARGETS),
                    metavar="T", help="subset of: %s" % " ".join(ALL_TARGETS))
    ap.add_argument("--output-root", default=None, help="default: release/ under the repo root")
    ap.add_argument("--ignore-stale", action="store_true",
                    help="downgrade the stale embedded page check to a warning")
    ap.add_argument("--no-ccache", action="store_true",
                    help="disable ccache; required when wifi_secret.h was just added or removed")
    ap.add_argument("--clean", action="store_true",
                    help="delete every build/ tree before building")
    ap.add_argument("--deep-clean", action="store_true",
                    help="--clean plus the ESP sdkconfig, dependencies.lock and managed_components; "
                         "everything compiles from scratch, several minutes per ESP")
    ap.add_argument("--clean-only", action="store_true", help="clean and exit")
    ap.add_argument("--list", action="store_true", help="show detected toolchains and exit")
    args = ap.parse_args()

    print("\nokhi release - by Dreg")
    print("repo: %s" % REPO)
    print("host: %s %s, python %s" % (platform.system(), platform.machine(), platform.python_version()))

    idf = find_idf()
    cmake = find_pico_tool("cmake", "OKHI_CMAKE")
    ninja = find_pico_tool("ninja", "OKHI_NINJA")
    describe_toolchains(idf, cmake, ninja)

    if args.list:
        return 0

    assert_crc32()

    unknown = [t for t in args.targets if t not in ALL_TARGETS]
    if unknown:
        raise SystemExit("unknown target(s): %s" % " ".join(unknown))

    if args.clean or args.deep_clean or args.clean_only:
        clean(args.targets, args.deep_clean)
        if args.clean_only:
            return 0
        # A deep clean removes the object ccache would match against anyway, and a
        # wifi_secret.h that appeared or vanished is invisible to it, so never let a
        # from-scratch build be served stale objects.
        args.no_ccache = True

    if not args.no_build:
        if not build_all(args.targets, idf, cmake, ninja, args.no_ccache):
            print("\nbuild failed, nothing was packaged")
            return 1

    if args.build_only:
        print("\nBuilt: %s" % " ".join(args.targets))
        return 0 if not errors else 1

    root = Path(args.output_root) if args.output_root else (REPO / "release")
    out_dir = root / time.strftime("%Y%m%d%H%M%S")
    out_dir.mkdir(parents=True, exist_ok=True)
    print("\nGenerating %s" % out_dir.relative_to(REPO) if str(out_dir).startswith(str(REPO)) else out_dir)

    package_all(out_dir, args.ignore_stale)

    print("")
    if warnings:
        print("%d warning(s):" % len(warnings))
        for w in warnings:
            print("  %s" % w)
        print("")
    if errors:
        print("=============================")
        print("FAILURES: %d issue(s)" % len(errors))
        print("=============================")
        for e in errors:
            print("  %s" % e)
        return 1

    print("All copies verified successfully.")
    print("Release ready: %s" % out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
