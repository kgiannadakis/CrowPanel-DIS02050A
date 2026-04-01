#!/usr/bin/env python3
"""
Build all three firmware images and flash them to the CrowPanel.

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

import subprocess, sys, os

if len(sys.argv) < 2 or sys.argv[1].startswith("--"):
    print("Usage: python flash_all.py <PORT> [--skip-build]")
    print("  e.g. python flash_all.py COM20")
    print("  e.g. python flash_all.py /dev/ttyUSB0")
    sys.exit(1)
PORT = sys.argv[1]
SKIP_BUILD = "--skip-build" in sys.argv
BAUD = "921600"

# Project directories and their PlatformIO environment names
PROJECTS = {
    "selector":   "boot_selector",
    "meshcore":   "crowpanel_v11_lvgl_chat",
    "meshtastic": "crowpanel-dis05020a-v11",
}

# Flash addresses (must match partitions_dualboot.csv)
FLASH_MAP = {
    "0x0000":   ("selector",   "bootloader.bin"),
    "0x8000":   ("selector",   None),              # partition table
    "0x10000":  ("selector",   "firmware.bin"),
    "0x110000": ("meshcore",   "firmware.bin"),
    "0x680000": ("meshtastic", "firmware.bin"),     # ota_1 partition
}

REPO_DIR = os.path.dirname(os.path.abspath(__file__))

def build_dir(project, env):
    return os.path.join(REPO_DIR, project, ".pio", "build", env)

def build_all():
    for proj, env in PROJECTS.items():
        proj_dir = os.path.join(REPO_DIR, proj)
        print(f"\n{'='*60}")
        print(f"Building {proj} (env: {env})")
        print(f"{'='*60}")
        subprocess.run(
            ["pio", "run", "-e", env],
            cwd=proj_dir,
            check=True
        )

def flash():
    # Build the esptool arguments
    args = [
        sys.executable, "-m", "esptool",
        "--chip", "esp32s3",
        "--port", PORT,
        "--baud", BAUD,
        "write_flash",
    ]

    # Partition table from selector project
    partition_csv = os.path.join(REPO_DIR, "selector", "partitions_dualboot.csv")

    for addr, (proj, filename) in FLASH_MAP.items():
        env = PROJECTS[proj]
        if filename is None:
            # Partition table binary
            bin_path = os.path.join(build_dir(proj, env), "partitions.bin")
        else:
            bin_path = os.path.join(build_dir(proj, env), filename)

        if not os.path.exists(bin_path):
            print(f"ERROR: {bin_path} not found. Build first.")
            sys.exit(1)

        args.extend([addr, bin_path])

    print(f"\n{'='*60}")
    print(f"Flashing to {PORT} at {BAUD} baud")
    print(f"{'='*60}")
    for i in range(0, len(args) - 7, 2):
        idx = i + 7
        if idx + 1 < len(args):
            print(f"  {args[idx]:10s} -> {os.path.basename(args[idx+1])}")

    subprocess.run(args, check=True)
    print("\nDone! Device will reboot into boot selector.")

if __name__ == "__main__":
    if not SKIP_BUILD:
        build_all()
    flash()
