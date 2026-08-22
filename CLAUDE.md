# okhi

Four firmware targets, two variants (`usb`, `ps2`) x two chips (`esp`, `rp`). Shared code lives in [firmware/com/](firmware/com/) and is `#include`d by all of them, so a change there rebuilds everything.

```
firmware/usb/esp   ESP32-C2, ESP-IDF        firmware/ps2/esp   ESP32-C2, ESP-IDF
firmware/usb/rp    RP2040,   Pico SDK       firmware/ps2/rp    RP2040,   Pico SDK
```

A fifth RP2040 project, [firmware/uart_bridge/](firmware/uart_bridge/), builds the USB-UART bridge both variants ship. It is not one of the four targets and shares only [firmware/com/com_rp_pins.h](firmware/com/com_rp_pins.h) with them, the board pin map: pure macros, no Pico SDK includes, so a project can pull it in without the rest of the okhi hardware layer. `com_rp.h` and `com_rp_hw.h` include it too, so a GPIO is written down once.

## Git

**Never run `git commit` or `git push`.** Only the user commits. Never rewrite the working tree either: no `git stash`, no `git reset --hard`, no `git checkout -- <file>` on files the user changed. To read an older revision use `git show HEAD:path` or `git diff`.

The user runs auto-commit tooling that creates commits titled `prev7` and periodically squashes with `git reset HEAD~N`. Expect the working tree to get committed out from under you mid-session; that is theirs, not a problem to fix.

## Toolchain locations on this machine

**ESP-IDF v6.0.2, installed via EIM (not the classic installer).** `export.ps1` does **not** work: it looks for the venv under `C:\Users\regue\.espressif\python_env\...`, which does not exist, and fails with `ESP-IDF Python virtual environment not found`. The activation script is:

```
C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
```

It sets `IDF_PATH=C:\Users\regue\esp\.espressif\v6.0.2\esp-idf`, `IDF_TOOLS_PATH=C:\Espressif\tools`, `IDF_PYTHON_ENV_PATH=C:\Espressif\tools\python\v6.0.2\venv`, and defines `idf.py` as an alias for the function `Invoke-idfpy`. The install is described in `C:\Espressif\tools\eim_idf.json`.

**Pico SDK 2.3.0**, under `C:\Users\regue\.pico-sdk\`:

| | |
|---|---|
| SDK | `C:\Users\regue\.pico-sdk\sdk\2.3.0` |
| toolchain | `C:\Users\regue\.pico-sdk\toolchain\15_2_Rel1` (arm-none-eabi gcc 15.2.1) |
| cmake | `C:\Users\regue\.pico-sdk\cmake\v4.3.4\bin\cmake.exe` |
| ninja | `C:\Users\regue\.pico-sdk\ninja\v1.13.2\ninja.exe` |
| picotool | `C:\Users\regue\.pico-sdk\picotool\2.3.0` |

None of these are on `PATH`; call them by absolute path. The `CMakeLists.txt` pulls `${USERPROFILE}/.pico-sdk/cmake/pico-vscode.cmake`, which resolves SDK, toolchain and picotool by itself, so **no `-DPICO_SDK_PATH` is needed**.

## How the user builds

The user does **not** use the command line. They build with the **official ESP-IDF VS Code extension** and the **official Raspberry Pi Pico VS Code extension**. The CLI commands below exist so this agent can verify builds; they must stay compatible with the extensions, never replace them.

They line up today: the Pico extension's `.vscode/settings.json` pins cmake `v4.3.4`, ninja `v1.13.2`, SDK `2.3.0` and toolchain `15_2_Rel1`, which is exactly what the CLI commands here use, so both produce the same `build/`.

Two things to respect when cleaning:

- **`build/.cmake/api/v1/query/client-vscode/query.json`** is written by the Pico extension to ask cmake for the file API (IntelliSense, target list). A CLI `cmake` configure does not create it, so wiping `build/` loses it. Recreate it and reconfigure so the `reply/` is regenerated:
  ```powershell
  $q = '{"requests":[{"kind":"cache","version":2},{"kind":"codemodel","version":2},{"kind":"toolchains","version":1},{"kind":"cmakeFiles","version":1}]}'
  $d = "<project>\build\.cmake\api\v1\query\client-vscode"
  New-Item -ItemType Directory -Force -Path $d | Out-Null
  [System.IO.File]::WriteAllText("$d\query.json", $q)
  ```
- **Never touch `.vscode/`.** That is the extensions' configuration, not build output. Nothing here needs to modify it.

**Known problem, not yet fixed:** the `.vscode/settings.json` of *both* ESP projects points at an ESP-IDF install that no longer exists. `idf.espIdfPathWin` is `C:\Users\regue\esp\v5.5\esp-idf` (the real one is v6.0.2 under `C:\Users\regue\esp\.espressif\v6.0.2\esp-idf`), `idf.toolsPathWin` and `idf.pythonInstallPath` point under `C:\Users\regue\.espressif`, which does not exist at all, and `clangd.arguments` has `--query-driver` aimed at an **xtensa** gcc even though the ESP32-C2 is riscv32, plus `--compile-commands-dir=c:\Users\regue\Desktop\okhi\build`, a path that does not exist either. Ask the user before changing any of it.

## Building

### ESP (both variants)

In PowerShell, dot-source the profile first. Shell state does not persist between tool calls, so activation and build must go in the **same** command:

```powershell
. "C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1" | Out-Null
Invoke-idfpy -C "c:\Users\regue\Desktop\okhi\firmware\usb\esp" build
```

Swap `usb` for `ps2` for the other variant. `-C <dir>` avoids having to `cd`. Use `Invoke-idfpy` rather than the `idf.py` alias; the dot in the alias name is fragile in non-interactive shells.

**ESP builds are slow.** ~30 s of CMake configure plus 752 compile steps: several minutes from clean. Run them with `run_in_background: true` and wait for the notification rather than polling.

### Dev WiFi build (`wifi_secret.h`) and the ccache trap that WILL waste your time

To make a board come up on a real network instead of its own AP, create
`firmware/<variant>/esp/src/wifi_secret.h` (gitignored) with `OKHI_DEV_STA_SSID`
and `OKHI_DEV_STA_PASS`. `wifi_client.h` (tracked, no secrets) pulls it in with
`#if __has_include("wifi_secret.h")`. See the README "Putting the implant on your
own WiFi" for the file body and the commit-safety rules.

**The trap (this cost a whole session):** `__has_include` creates a dependency
on a file that did *not* exist at the previous compile, so **neither ninja nor
ccache re-runs `okhi.c` when you add or remove `wifi_secret.h`.** ESP-IDF enables
ccache (`C:\Espressif\tools\ccache\...`); its direct mode hashes only the headers
the *last* compile recorded, misses the new `wifi_secret.h`, and returns the
STALE object — so the build "succeeds", the `.obj` mtime updates, but the secret
is NOT compiled in and the board still comes up as an AP. Touching `okhi.c` alone
is not enough: ninja recompiles, ccache serves the stale object anyway.

**The reliable way to pick up a `wifi_secret.h` add/remove/change:**

```powershell
$env:IDF_CCACHE_ENABLE = "0"
Remove-Item "c:\Users\regue\Desktop\okhi\firmware\usb\esp\build\esp-idf\main\CMakeFiles\__idf_main.dir\okhi.c.obj" -Force -ErrorAction SilentlyContinue
. "C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1" | Out-Null
$env:IDF_CCACHE_ENABLE = "0"
Invoke-idfpy -C "c:\Users\regue\Desktop\okhi\firmware\usb\esp" build
```

(Set `IDF_CCACHE_ENABLE=0` **both** before and after dot-sourcing the profile; the
profile can reset the environment. `ccache -C` clears the whole cache if you
prefer that over deleting the one object.) A full `build/` wipe also works and is
the safest, just slower.

**ALWAYS verify, never trust the exit code.** The build prints a `#pragma message`
"wifi_secret.h is compiled in ..." when the secret is really in the translation
unit. Then confirm the image carries it. Do NOT rely on a plain string grep: a
short password (<= 8 bytes) is emitted as immediate word-stores, not a contiguous
rodata literal, so `grep pass okhi.bin` finds nothing even on a correct build.
The SSID (longer) does appear as a literal, so it is the reliable canary:

```bash
python -c "print(open('firmware/usb/esp/build/okhi.bin','rb').read().count(b'YourSSID'))"   # >=1 means the STA config is in (use your real SSID)
```

To prove a short password is in, disassemble and look for its bytes as immediates
(`riscv32-esp-elf-objdump -d okhi.elf`; e.g. "7mcc" -> `# 63636d37`, "AtSj" ->
`# 6a537441`).

The RP/Pico build has **no ccache** (checked: `CMakeCache.txt` has no
`RULE_LAUNCH_COMPILE`/`COMPILER_LAUNCHER`) and no `__has_include` conditional
headers, so it does not suffer this. A normal header edit like `com_rp_ota.h` is
tracked by ninja deps and rebuilt correctly (the flash size moves when it should).

### RP (both variants)

Configure once, then build. Two explicit `-D` flags are required because neither cmake nor ninja is on `PATH`:

```powershell
& "C:\Users\regue\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" -G Ninja `
  -S "c:\Users\regue\Desktop\okhi\firmware\usb\rp" `
  -B "c:\Users\regue\Desktop\okhi\firmware\usb\rp\build" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_MAKE_PROGRAM="C:/Users/regue/.pico-sdk/ninja/v1.13.2/ninja.exe"

& "C:\Users\regue\.pico-sdk\ninja\v1.13.2\ninja.exe" -C "c:\Users\regue\Desktop\okhi\firmware\usb\rp\build"
```

`CMAKE_BUILD_TYPE=Debug` matches what the committed caches use; the project sets `PICO_DEOPTIMIZED_DEBUG 1`, so Debug is the intended build type, not a debugging-only mode. Takes seconds to a minute.

### uart_bridge

Same two commands with `firmware\uart_bridge` as both source and build root. It is a plain Pico SDK 2.3.0 project like the two `rp` targets, so the Pico VS Code extension opens it unchanged, but it deliberately differs from them in two ways:

- **No `PICO_DEOPTIMIZED_DEBUG`.** Debug therefore compiles at `-Og`, not `-O0`. The RX FIFOs are off, so the bridge takes one interrupt per byte, and `upload_firmware.bat` drives esptool at 921600 baud.
- **`set(LOG 0)` before `pico_sdk_init()`.** TinyUSB's `family.cmake` switches `CFG_TUSB_DEBUG` on by itself whenever `CMAKE_BUILD_TYPE` is `Debug`, which links its logging into the USB task. `LOG` is the only knob it checks first. Removing it saves 1176 B of flash and keeps the binary equivalent to the Release build that produced the original image.

## Extreme full clean

`idf.py fullclean` is **not** enough. It empties `build/` but keeps `sdkconfig`, and stale `sdkconfig` is exactly what has caused breakage here before. The reliable reset is to delete state outright, then build:

| what | why |
|---|---|
| `build/` | all generated output, cmake cache, ninja logs |
| `sdkconfig` | regenerated from `sdkconfig.defaults`; stale copies survive `fullclean` |
| `managed_components/` | component-manager downloads (absent today, appears if a dependency is added) |
| `dependencies.lock` | pins managed components (same) |

Deleting `build/` also removes the need for `fullclean`, so there is no reason to run both. Keep `sdkconfig.defaults`, `sdkconfig.ci*` and `partitions.csv`: those are versioned and are the source of truth. `sdkconfig` and `sdkconfig.old` are generated and no longer tracked, so deleting them costs nothing and shows up in no diff.

Reset all four and rebuild:

```powershell
$r = "c:\Users\regue\Desktop\okhi\firmware"
foreach ($t in @("usb\esp","usb\rp","ps2\esp","ps2\rp","uart_bridge")) {
  Remove-Item "$r\$t\build" -Recurse -Force -ErrorAction SilentlyContinue
}
foreach ($t in @("usb\esp","ps2\esp")) {
  Remove-Item "$r\$t\sdkconfig","$r\$t\dependencies.lock" -Force -ErrorAction SilentlyContinue
  Remove-Item "$r\$t\managed_components" -Recurse -Force -ErrorAction SilentlyContinue
}

# ESP, several minutes each, run in background
. "C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1" | Out-Null
foreach ($t in @("usb","ps2")) { Invoke-idfpy -C "$r\$t\esp" build }

# RP, seconds each
$cm = "C:\Users\regue\.pico-sdk\cmake\v4.3.4\bin\cmake.exe"
$nj = "C:\Users\regue\.pico-sdk\ninja\v1.13.2\ninja.exe"
foreach ($t in @("usb\rp","ps2\rp","uart_bridge")) {
  & $cm -G Ninja -S "$r\$t" -B "$r\$t\build" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MAKE_PROGRAM="C:/Users/regue/.pico-sdk/ninja/v1.13.2/ninja.exe"
  & $nj -C "$r\$t\build"
}
```

Verified end to end from a completely wiped state: all four rebuild, both ESP binaries come back at exactly the sizes below with zero warnings, both RP binaries byte-for-byte the same size, and both `sdkconfig` files regenerate **byte-identical** to the copies taken beforehand, with `CONFIG_XTAL_FREQ_26`, `CONFIG_ESP32C2_REV_MIN_1`, `CONFIG_ESPTOOLPY_FLASHSIZE_4MB` and `CONFIG_PARTITION_TABLE_CUSTOM` intact. `sdkconfig.defaults` is the source of truth and the pinning works; read [firmware/usb/esp/IMPORTANT.txt](firmware/usb/esp/IMPORTANT.txt) before touching any of it, since 26 MHz crystal, minimum revision v1.0, 4 MB flash and the custom partition table all decide whether field boards accept the image.

After wiping an RP `build/`, restore the query file the Pico VS Code extension relies on (see below) before handing the tree back.

A lighter RP clean, keeping the cmake configure, is `ninja -C "<project>\build" -t clean`. It drops 81 outputs and rebuilds to identical sizes, but it does not clear the cmake cache, so it is not a substitute when something is actually wrong.

## Expected output

Sizes below are from a verified clean build of every target. Treat a deviation as a real change, not noise.

| target | size | notes |
|---|---|---|
| `usb/esp` | `okhi.bin` 0xceb50 (846 672 B), 41% of app partition free | 752 steps, **0 warnings** |
| `ps2/esp` | `okhi.bin` 0xcacf0, 42% free | 752 steps, **0 warnings** |
| `usb/rp` | FLASH 56 916 B (2.71%), RAM 199 668 B (76.17%) | 76 steps, 10 warnings, all pre-existing |
| `ps2/rp` | FLASH 53 824 B (2.57%), RAM 94 108 B (35.90%) | 76 steps, **0 warnings** |
| `uart_bridge` | FLASH 33 100 B (1.58%), RAM 44 788 B (17.09%), `uart_bridge.uf2` 66 560 B | 91 steps, 2 warnings, both pre-existing |

The 10 `usb/rp` warnings are long-standing and unrelated to recent work: unused `capture_*_str` / `display_*_str` tables, unused `ftime` and `readed_last`, a `-Wpointer-sign` on `my_spi_to_esp_write_blocking(pkts, ...)` at [okhi.c:621](firmware/usb/rp/okhi.c#L621), and a discarded `volatile` in `GetLastDbuffAddr` at [okhi.c:1050](firmware/usb/rp/okhi.c#L1050). `ps2/rp` has none, so they are specific to the usb source.

The 2 `uart_bridge` warnings are dead locals inherited from upstream: `serial` at [usb-descriptors.c:110](firmware/uart_bridge/usb-descriptors.c#L110), and `con` at [uart-bridge.c:197](firmware/uart_bridge/uart-bridge.c#L197), unused since the Dreg mod stopped filtering on `tud_cdc_n_connected()`. Left alone so the code stays as it shipped; only the file headers were rewritten, and the binary is byte-identical across that change.

## Releases and what the repo must keep

`make_release.bat` **builds nothing**. It copies existing artifacts into a timestamped folder and calls `stuff/make_ota_package.ps1` to produce `okhi_{USB,PS2}_ota.pkg`. Build all four targets plus `uart_bridge` first, then run it.

The remote doubles as a backup and as an informal nightly-build channel, so users pull firmware straight from it. That means **a fresh clone must be able to run `make_release.bat` with no build step**. Everything it reads has to stay versioned:

| per variant (`usb`, `ps2`) | shared |
|---|---|
| `firmware/<v>/esp/esptool.exe` | `stuff/okhi_reset_flash.uf2` |
| `firmware/<v>/esp/upload_firmware.bat` | `firmware/uart_bridge/build/uart_bridge.uf2` |
| `firmware/<v>/esp/build/okhi.bin` | `stuff/OTA_INSTRUCTIONS.txt` |
| `firmware/<v>/esp/build/bootloader/bootloader.bin` | `stuff/make_ota_package.ps1` |
| `firmware/<v>/esp/build/partition_table/partition-table.bin` | `stuff/last_esptool/` (24 files) |
| `firmware/<v>/esp/build/storage.bin` | |
| `firmware/<v>/rp/build/okhi.uf2` | |
| `firmware/<v>/rp/build/okhi.bin` | |
| `firmware/<v>/PROGRAM_INSTRUCTIONS.txt` | |

`uart_bridge.uf2` used to come from `stuff/pico-uart-bridge-dregmod/build/`, a Linux out-of-tree build that nothing here could reproduce. It is now built from [firmware/uart_bridge/](firmware/uart_bridge/) like every other target, and `make_release.bat` reads it from there.

`.vscode/` is versioned on purpose: the user builds through the VS Code extensions, so those 24 files belong in the remote.

The root `.gitignore` denies the contents of `firmware/*/{esp,rp}/build/` and `firmware/uart_bridge/build/`, and re-admits the artifacts by name, so build junk stays out while the flashable images and debug symbols stay in. Kept per variant: ESP `okhi.{bin,elf,map}`, `storage.bin`, `ota_data_initial.bin`, `bootloader/bootloader.{bin,elf,map}`, `partition_table/partition-table.bin`; RP `okhi.{uf2,bin,elf,elf.map,hex,dis}`; and `uart_bridge.{uf2,bin,elf,elf.map,hex,dis}`. That is 36 files in total. It also drops the generated `sdkconfig` and `sdkconfig.old`.

This was cleaned up in commit `4052fc1`, which untracked 2335 intermediates and the four `sdkconfig` files, taking the tree from 6950 tracked files to 4617.

**Do not propose rewriting history to reclaim that space.** It was measured and rejected on 2026-08-10. Those 2335 files are ~291 MB on disk but only 66 MB inside the pack, because object files delta-compress well across rebuilds. Of 924 MB of blobs in history, only 136 MB (14.7%) is dead weight not reachable from HEAD, and 78 MB of that is one deleted Saleae capture, `stuff/PS2_GLITCH_CAPTURE.sal`. A `git filter-repo` rewrite would take a clone from 1.38 GiB to about 1.24 GiB, roughly 10%, in exchange for changing every SHA, invalidating existing clones and disturbing a remote that serves as the user's backup. Not worth it.

The real weight is current content the user wants: `stuff/last_esptool` (320 MB in the pack, 403 MB on disk, 24 esptool binaries across 6 platforms, and a release input) plus roughly 200 MB of datasheet PDFs under `stuff/`. Measure before proposing anything here:

```bash
git rev-list --objects --all | git cat-file --batch-check='%(objecttype) %(objectsize:disk) %(rest)'
```

Verified against the pushed commit by exporting it with `git archive` and running `make_release.bat` on the result: exit 0, all copies verified, both `.pkg` files produced with matching CRCs.

**When adding a new build artifact to a release, add it to `.gitignore` too**, or it will be silently absent from a fresh clone and `make_release.bat` will fail with `[MISSING]` for everyone but the machine that built it. That is exactly how `firmware/ps2/rp/build/okhi.uf2` went missing from the remote.

## Gotchas

- **A build carrying `wifi_secret.h` must never be committed.** That header is
  gitignored, but `firmware/*/esp/build/okhi.{bin,elf}` are tracked on purpose
  so a fresh clone can run `make_release.bat`, and a build made with the secret
  present has the SSID and password in those binaries in clear text. The build
  emits a `#warning` when it happens. Before committing or releasing, remove the
  header, rebuild, and confirm with `git grep -l "<the ssid>" -- .`.

- **An OTA update WIPES NVS, so a STA board drops to AP unless creds are compiled in.** The ESP stores the running image's build stamp in NVS and, on every boot, erases the WHOLE of NVS on a stamp mismatch (documented, on purpose). Any OTA carries a new stamp, so after it the stored WiFi settings are gone and the board falls back to its factory default: an access point (`USB_xxxx`/`PS2_xxxx`, pass `1234567890`, `http://192.168.4.1/`). If the board was reached over the LAN in STA mode, **it will disappear from the LAN after the OTA** and the ESP validation step (load the page within 5 min or it rolls back) then has to happen at `192.168.4.1`. To keep a dev board on your network across OTAs, compile in `wifi_secret.h` (see the dev build section): after the NVS wipe it auto-rejoins from the compiled default and usually gets the same DHCP IP. **Warn the user before OTA-ing a STA board that has no compiled creds.**
- **`export.ps1` is a dead end.** See above. This is the single most likely way to waste time here.
- **The two RP `okhi` binaries are not byte-reproducible.** Both `okhi.c` files print `__DATE__` and `__TIME__` at startup, and `RP_IDENTITY` in [com_rp_ota.h](firmware/com/com_rp_ota.h#L66) bakes them in again, so any recompile moves 10 bytes even when nothing changed. Comparing hashes to prove a refactor was a no-op does not work here; `cmp -l old new` does, and the diff should land entirely inside those two time strings. `uart_bridge` has no timestamp and *is* byte-reproducible, so for that one the hash is a valid check.
- **`firmware/usb/rp/build/.ninja_log` can get corrupted** (`ninja: warning: premature end of file; recovering`). Ninja then rebuilds all 14 targets every time instead of going incremental. Delete the file to fix it.
- **Build directories are tracked in git**, which makes an extreme clean look alarming in `git status`. Both `rp` dirs carry a `.gitignore` containing `build`, but `usb/rp/build` was committed before it was added, so the ignore has no effect there. After a full wipe and rebuild the only tracked files that do not come back are `build/log/idf_py_std{out,err}_output_<PID>` (idf.py names them after the process ID, so a new run always writes new ones) and the Pico `query.json` above. Everything else regenerates. Report the leftovers, do not commit anything.
- **`stuff/` is out of scope for building.** It holds old and experimental projects (`clock`, `pico_clock_calc`, `testfirmware`, `espcode`, `usb-sniffer-lite-dregmod`, `oldusb`) with their own committed `build/` dirs. Do not clean or build them unless asked. Some of its files are release inputs, though, so never delete from it.

## Code style

No comments in firmware code. Everything in fluent English.
