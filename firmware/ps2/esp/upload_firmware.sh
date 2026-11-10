#!/usr/bin/env bash
# okhi by Dreg: the ESP32-C2 firmware of this release from Linux or macOS, the same steps as
# upload_firmware.bat on Windows, and the RP2040's .uf2 files onto its RPI-RP2 drive.
#
#   bash upload_firmware.sh [serial port]     erase the ESP32-C2, then write bootloader.bin,
#                                             partition-table.bin, okhi.bin and storage.bin
#   bash upload_firmware.sh uf2 <file.uf2>    copy the file to the RPI-RP2 drive once it shows up
#   bash upload_firmware.sh --help            these notes
#
# The whole update, as in the README's "Firmware update":
#   1. Hold BOOT while you plug the implant probe in (implant in the PROG zone): the RP2040 shows
#      up as the drive RPI-RP2.             bash upload_firmware.sh uf2 uart_bridge.uf2
#   2. Unplug it and plug it back without BOOT: the RP2040 is now a USB to UART bridge, 2e8a:000a,
#      a /dev/ttyACM<n> port on Linux, /dev/cu.usbmodem... on macOS (not tested through the bridge).
#                                           bash upload_firmware.sh /dev/ttyACM0
#   3. Unplug it, hold BOOT, plug it back.  bash upload_firmware.sh uf2 okhi.uf2
#
# On the development board, whose ESP8684-DevKitM-1 has a CP210x of its own, step 2 uses that
# port (/dev/ttyUSB<n>, or its /dev/serial/by-id/usb-Silicon_Labs_CP2102N_... name), step 1 is
# not needed, and the RP2040's okhi.uf2 goes on separately. There the RP2040 running okhi stays
# off the ESP for 50 s after esptool's first reset; the erase and the write run back to back,
# about 28 s.
#
# Linux, once:
#   - On Ubuntu the serial port needs the dialout group: sudo usermod -aG dialout "$USER", then
#     log out and in again (ls -l on the port shows its group).
#   - ModemManager may open a new ttyACM port to probe it while esptool needs it. To keep it off
#     the bridge, put this line in /etc/udev/rules.d/70-okhi.rules and run
#     sudo udevadm control --reload:
#       SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000a", ENV{ID_MM_DEVICE_IGNORE}="1"
#   - If a USB serial port vanishes right after it appears and the kernel log (journalctl -k)
#     names brltty, brltty took it: sudo apt remove brltty.
#   - On a desktop the RPI-RP2 drive should mount by itself (/media/$USER/RPI-RP2; not tried with
#     a real drive). Without a desktop: lsblk -o NAME,LABEL shows it, udisksctl mount -b /dev/sdX1
#     mounts it, or set RPI_RP2=<mount point>.
#   - esptool: this release's ../last_esptool/esptool-<os>-<arch>/esptool, made executable here
#     (the release keeps it without the execute bit), else esptool or esptool.py on the PATH (pip
#     install esptool), or ESPTOOL=<path>. BAUD=460800 for a link that fails at 921600.

set -u
cd "$(dirname "$0")" || exit 1

BAUD=${BAUD:-921600}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

usage() {
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
}

bundled_esptool() {
    local o a dir
    case "$(uname -s)" in
        Linux) o=linux ;;
        Darwin) o=macos ;;
        *) return 1 ;;
    esac
    case "$(uname -m)" in
        x86_64 | amd64) a=amd64 ;;
        aarch64 | arm64) if [ "$o" = macos ]; then a=arm64; else a=aarch64; fi ;;
        armv7*) a=armv7 ;;
        *) return 1 ;;
    esac
    dir=../last_esptool/esptool-$o-$a
    [ -f "$dir/esptool" ] || return 1
    if [ ! -x "$dir/esptool" ] && ! chmod +x "$dir/esptool" 2>/dev/null; then
        # a read-only copy (a mounted image): run it from a writable place
        local tmp
        tmp=$(mktemp -d) || return 1
        cp "$dir/esptool" "$tmp/esptool" && chmod +x "$tmp/esptool" || return 1
        echo "$tmp/esptool"
        return 0
    fi
    echo "$dir/esptool"
}

find_esptool() {
    if [ -n "${ESPTOOL:-}" ]; then
        echo "$ESPTOOL"
        return 0
    fi
    bundled_esptool && return 0
    command -v esptool 2>/dev/null && return 0
    command -v esptool.py 2>/dev/null && return 0
    return 1
}

list_ports() {
    local p found=0
    if [ -d /dev/serial/by-id ]; then
        for p in /dev/serial/by-id/*; do
            [ -e "$p" ] || continue
            echo "  $p -> $(readlink -f "$p")"
            found=1
        done
    fi
    for p in /dev/ttyACM* /dev/ttyUSB* /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART*; do
        [ -e "$p" ] || continue
        echo "  $p"
        found=1
    done
    [ $found = 1 ] || echo "  (none: is the implant plugged in, and running uart_bridge.uf2?)"
}

copy_uf2() {
    local f=$1 d i who
    [ -f "$f" ] || die "no file $f"
    who=${USER:-$(id -un)}
    echo "Waiting up to 120 s for the RPI-RP2 drive (hold BOOT while you plug the implant probe in)..."
    for i in $(seq 120); do
        for d in "${RPI_RP2:-}" "/media/$who/RPI-RP2" "/run/media/$who/RPI-RP2" /Volumes/RPI-RP2; do
            if [ -n "$d" ] && [ -f "$d/INFO_UF2.TXT" ]; then
                echo "Copying $f to $d"
                cp "$f" "$d/" || die "copy to $d failed"
                sync
                echo "Done: the drive ejects by itself and the RP2040 starts the new firmware."
                return 0
            fi
        done
        sleep 1
    done
    die "no RPI-RP2 drive mounted in 120 s (see --help: mounting it by hand)"
}

case "${1:-}" in
    -h | --help)
        usage
        exit 0
        ;;
    uf2)
        [ $# -ge 2 ] || die "usage: bash upload_firmware.sh uf2 <file.uf2>"
        copy_uf2 "$2"
        exit 0
        ;;
esac

echo "By Dreg"
echo "uart_bridge.uf2 prior to firmware version v5 requires the esptool version (v4.7.0) included in"
echo "release packages of that time. Starting from firmware version v5, uart_bridge.uf2 works with the"
echo "latest esptool: https://github.com/espressif/esptool/issues/1119"

for f in bootloader.bin partition-table.bin okhi.bin storage.bin; do
    [ -f "$f" ] || die "$f is missing: run this script from the usb or ps2 folder of a release"
done

ESP=$(find_esptool) || die "no esptool: this release's last_esptool folder is missing, and none is on the PATH (pip install esptool, or ESPTOOL=<path>)"
VER=$("$ESP" version 2>/dev/null | tail -n 1 | sed 's/^[^0-9]*//')
MAJOR=${VER%%.*}
echo "esptool: $ESP (version ${VER:-unknown})"
# esptool v4 spells its commands and options with an underscore, v5 with a dash
if [ -n "$MAJOR" ] && [ "$MAJOR" -lt 5 ] 2>/dev/null; then
    ERASE=erase_flash
    WRITE=write_flash
    MODE=--flash_mode FREQ=--flash_freq SIZE=--flash_size
else
    ERASE=erase-flash
    WRITE=write-flash
    MODE=--flash-mode FREQ=--flash-freq SIZE=--flash-size
fi

PORT=${1:-}
if [ -z "$PORT" ]; then
    echo "Available serial ports:"
    list_ports
    while [ -z "$PORT" ]; do
        printf "Please enter the serial port (e.g. /dev/ttyACM0): "
        read -r PORT || exit 1
    done
fi
[ -e "$PORT" ] || die "no port $PORT"
if [ ! -r "$PORT" ] || [ ! -w "$PORT" ]; then
    die "no read and write access to $PORT ($(ls -l "$PORT" 2>/dev/null | awk '{print $1, $3, $4}')): sudo usermod -aG dialout \"\$USER\", then log out and in"
fi

echo "Erasing the whole flash first. This is required: the OTA partition table put"
echo "otadata at 0xd000, where the old nvs partition used to live, so leftover data"
echo "would make the bootloader read garbage from the boot selector."
"$ESP" --port "$PORT" --chip esp32c2 --baud "$BAUD" "$ERASE" ||
    die "erase failed, nothing written (try BAUD=460800, and see --help for ModemManager and brltty)"
# esptool v5 can end a good write with "Invalid head of packet (0x00)" and a nonzero exit code
# (the README's "Two error messages that are harmless"): the four verified hashes decide
LOG=$(mktemp) || die "mktemp failed"
"$ESP" --port "$PORT" --chip esp32c2 --baud "$BAUD" "$WRITE" -z "$MODE" dio "$FREQ" 60m "$SIZE" 4MB \
    0x0000 bootloader.bin 0x8000 partition-table.bin 0x10000 okhi.bin 0x2d0000 storage.bin 2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}
HASHES=$(grep -c "Hash of data verified" "$LOG")
rm -f "$LOG"
[ "$HASHES" -ge 4 ] || die "write failed, $HASHES of 4 images verified: run it again (the erase comes first each time)"
[ "$RC" = 0 ] || echo "esptool exited with $RC after all 4 images verified: the harmless error the README describes."
echo "Done: the ESP32-C2 is programmed. Its web page's ESP line must show this release's MANIFEST.txt value."
