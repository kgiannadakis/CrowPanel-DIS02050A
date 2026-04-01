#!/usr/bin/env python3
"""
Build all three firmwares and flash the dual-boot system to the CrowPanel.

Uses the repo-root partitions.bin and the Meshtastic bootloader.

Requires: Python 3 + PlatformIO CLI (pio) + esptool
          (all included with a standard PlatformIO installation)

Usage:
    python flash_all.py <PORT>               (build + flash)
    python flash_all.py <PORT> --skip-build  (flash only, no rebuild)

Examples:
    python flash_all.py COM20               (Windows)
    python flash_all.py /dev/ttyUSB0        (Linux)
    python flash_all.py /dev/cu.usbserial   (macOS)
"""

import subprocess, sys, os, glob

if len(sys.argv) < 2 or sys.argv[1].startswith("--"):
    print("Usage: python flash_all.py <PORT> [--skip-build]")
    print("  e.g. python flash_all.py COM20")
    print("  e.g. python flash_all.py /dev/ttyUSB0")
    sys.exit(1)

PORT = sys.argv[1]
SKIP_BUILD = "--skip-build" in sys.argv
BAUD = "921600"

REPO_DIR = os.path.dirname(os.path.abspath(__file__))

BUILDS = [
    ("selector",   "boot_selector"),
    ("meshcore",   "crowpanel_v11_lvgl_chat"),
    ("meshtastic", "crowpanel-dis05020a-v11"),
]


def find_firmware(proj, env):
    build_dir = os.path.join(REPO_DIR, proj, ".pio", "build", env)
    if not os.path.isdir(build_dir):
        return None
    for f in sorted(os.listdir(build_dir)):
        if f.startswith("firmware") and f.endswith(".bin") and "merged" not in f:
            return os.path.join(build_dir, f)
    return None


def build_all():
    for proj, env in BUILDS:
        proj_dir = os.path.join(REPO_DIR, proj)
        print(f"\n{'='*60}")
        print(f"  Building {proj} (env: {env})")
        print(f"{'='*60}")
        subprocess.run(["pio", "run", "-e", env], cwd=proj_dir, check=True)


def flash():
    # Repo-root partitions.bin
    partitions = os.path.join(REPO_DIR, "partitions.bin")
    if not os.path.isfile(partitions):
        print("ERROR: partitions.bin not found in repo root.")
        sys.exit(1)

    # Bootloader from Meshtastic build
    mt_build = os.path.join(REPO_DIR, "meshtastic", ".pio", "build", "crowpanel-dis05020a-v11")
    bootloader = os.path.join(mt_build, "bootloader.bin")
    if not os.path.isfile(bootloader):
        print("ERROR: bootloader.bin not found in meshtastic build. Build meshtastic first.")
        sys.exit(1)

    # Firmware binaries
    selector_fw  = find_firmware("selector",   "boot_selector")
    meshcore_fw  = find_firmware("meshcore",   "crowpanel_v11_lvgl_chat")
    meshtastic_fw = find_firmware("meshtastic", "crowpanel-dis05020a-v11")

    for name, fw in [("selector", selector_fw), ("meshcore", meshcore_fw), ("meshtastic", meshtastic_fw)]:
        if not fw:
            print(f"ERROR: No firmware found for {name}. Build first.")
            sys.exit(1)

    print(f"\n{'='*60}")
    print(f"  Flashing to {PORT} at {BAUD} baud")
    print(f"{'='*60}")
    print(f"  0x0000   -> bootloader.bin (from meshtastic)")
    print(f"  0x8000   -> partitions.bin (from repo root)")
    print(f"  0x10000  -> {os.path.basename(selector_fw)}")
    print(f"  0x110000 -> {os.path.basename(meshcore_fw)}")
    print(f"  0x660000 -> {os.path.basename(meshtastic_fw)}")

    subprocess.run([
        sys.executable, "-m", "esptool",
        "--chip", "esp32s3", "--port", PORT, "--baud", BAUD,
        "write_flash",
        "0x0000",   bootloader,
        "0x8000",   partitions,
        "0x10000",  selector_fw,
        "0x110000", meshcore_fw,
        "0x660000", meshtastic_fw,
    ], check=True)

    print("\nDone! Power-cycle to enter the boot selector.")


if __name__ == "__main__":
    if not SKIP_BUILD:
        build_all()
    flash()
