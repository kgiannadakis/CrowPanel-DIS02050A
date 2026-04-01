#!/usr/bin/env python3
"""
flash_all.py — Build and flash the complete dual-boot system to CrowPanel DIS02050A.

Requires: Python 3 + PlatformIO CLI (pio) + esptool
          (all included with a standard PlatformIO installation)

Usage:
    python flash_all.py <PORT>                          (build + flash)
    python flash_all.py <PORT> --skip-build             (flash only, no rebuild)
    python flash_all.py <PORT> --skip-build --erase     (erase flash first, then flash)

Examples:
    python flash_all.py COM20               (Windows)
    python flash_all.py /dev/ttyUSB0        (Linux)
    python flash_all.py /dev/cu.usbserial   (macOS)
"""

import subprocess, sys, os

if len(sys.argv) < 2 or sys.argv[1].startswith("--"):
    print("Usage: python flash_all.py <PORT> [--skip-build] [--erase]")
    print("  e.g. python flash_all.py COM20")
    print("  e.g. python flash_all.py /dev/ttyUSB0")
    sys.exit(1)

PORT = sys.argv[1]
SKIP_BUILD = "--skip-build" in sys.argv
ERASE = "--erase" in sys.argv
BAUD = "921600"

REPO_DIR = os.path.dirname(os.path.abspath(__file__))

BUILDS = {
    "selector":   {"dir": "selector",   "env": "boot_selector"},
    "meshcore":   {"dir": "meshcore",   "env": "crowpanel_v11_lvgl_chat"},
    "meshtastic": {"dir": "meshtastic", "env": "elecrow-adv1-43-50-70-tft"},
}

# Flash addresses (must match partition table)
ADDR_BOOTLOADER = "0x0000"
ADDR_PARTITIONS = "0x8000"
ADDR_FACTORY    = "0x10000"    # boot selector
ADDR_OTA_0      = "0x110000"   # MeshCore
ADDR_OTA_1      = "0x660000"   # Meshtastic


def find_firmware(proj_dir, env_name):
    build_dir = os.path.join(proj_dir, ".pio", "build", env_name)
    if not os.path.isdir(build_dir):
        return None
    for f in sorted(os.listdir(build_dir)):
        if f.startswith("firmware") and f.endswith(".bin") and "merged" not in f:
            return os.path.join(build_dir, f)
    return None


def find_artifact(proj_dir, env_name, filename):
    path = os.path.join(proj_dir, ".pio", "build", env_name, filename)
    return path if os.path.isfile(path) else None


def build_all():
    for name, info in BUILDS.items():
        proj_dir = os.path.join(REPO_DIR, info["dir"])
        env = info["env"]
        print(f"\n{'='*60}")
        print(f"  Building {name} (env: {env})")
        print(f"{'='*60}")
        subprocess.run(["pio", "run", "-e", env], cwd=proj_dir, check=True)


def flash():
    for name, info in BUILDS.items():
        proj_dir = os.path.join(REPO_DIR, info["dir"])
        fw = find_firmware(proj_dir, info["env"])
        if fw is None:
            print(f"ERROR: No firmware found for {name}. Build first.")
            sys.exit(1)
        info["firmware"] = fw
        print(f"  {name:12s} -> {os.path.basename(fw)}")

    sel_dir = os.path.join(REPO_DIR, "selector")
    sel_env = BUILDS["selector"]["env"]
    bootloader = find_artifact(sel_dir, sel_env, "bootloader.bin")
    partitions = find_artifact(sel_dir, sel_env, "partitions.bin")

    if not bootloader:
        print("ERROR: bootloader.bin not found. Build selector first.")
        sys.exit(1)
    if not partitions:
        print("ERROR: partitions.bin not found. Build selector first.")
        sys.exit(1)

    cmd_base = [sys.executable, "-m", "esptool",
                "--chip", "esp32s3", "--port", PORT, "--baud", BAUD]

    if ERASE:
        print(f"\n  Erasing flash...")
        subprocess.run(cmd_base + ["erase_flash"], check=True)

    print(f"\n{'='*60}")
    print(f"  Flashing to {PORT}")
    print(f"{'='*60}")
    print(f"  {ADDR_BOOTLOADER}   bootloader.bin")
    print(f"  {ADDR_PARTITIONS}   partitions.bin")
    print(f"  {ADDR_FACTORY}  selector")
    print(f"  {ADDR_OTA_0} meshcore")
    print(f"  {ADDR_OTA_1} meshtastic")

    subprocess.run(cmd_base + [
        "write_flash",
        ADDR_BOOTLOADER, bootloader,
        ADDR_PARTITIONS, partitions,
        ADDR_FACTORY,    BUILDS["selector"]["firmware"],
        ADDR_OTA_0,      BUILDS["meshcore"]["firmware"],
        ADDR_OTA_1,      BUILDS["meshtastic"]["firmware"],
    ], check=True)

    print("\nDone! Power-cycle to enter the boot selector.")


if __name__ == "__main__":
    if not SKIP_BUILD:
        build_all()
    flash()
