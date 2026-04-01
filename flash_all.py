#!/usr/bin/env python3
"""
flash_all.py — Build and flash the complete dual-boot system to CrowPanel DIS02050A.

Builds all three firmwares (selector, meshcore, meshtastic) then flashes them
to the correct partition addresses using esptool.

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

# Build environments
BUILDS = {
    "selector": {
        "dir": "selector",
        "env": "boot_selector",
        "firmware": None,  # resolved after build
    },
    "meshcore": {
        "dir": "meshcore",
        "env": "crowpanel_v11_lvgl_chat",
        "firmware": None,
    },
    "meshtastic": {
        "dir": "meshtastic",
        "env": "crowpanel-dis05020a-v11",
        "firmware": None,
    },
}

# Flash addresses (must match partitions_dualboot_16MB.csv)
ADDR_BOOTLOADER = "0x0000"
ADDR_PARTITIONS = "0x8000"
ADDR_FACTORY    = "0x10000"    # boot selector
ADDR_OTA_0      = "0x120000"   # MeshCore
ADDR_OTA_1      = "0x620000"   # Meshtastic


def find_firmware(proj_dir, env_name):
    """Find the firmware binary in a PlatformIO build directory."""
    build_dir = os.path.join(proj_dir, ".pio", "build", env_name)
    if not os.path.isdir(build_dir):
        return None
    # Meshtastic uses a versioned name; others use firmware.bin
    for f in os.listdir(build_dir):
        if f.startswith("firmware") and f.endswith(".bin") and "merged" not in f:
            return os.path.join(build_dir, f)
    return None


def find_build_artifact(proj_dir, env_name, filename):
    """Find a specific build artifact (bootloader.bin, partitions.bin)."""
    path = os.path.join(proj_dir, ".pio", "build", env_name, filename)
    return path if os.path.isfile(path) else None


def build_all():
    for name, info in BUILDS.items():
        proj_dir = os.path.join(REPO_DIR, info["dir"])
        env = info["env"]
        print(f"\n{'='*60}")
        print(f"  Building {name} (env: {env})")
        print(f"{'='*60}")
        subprocess.run(
            ["pio", "run", "-e", env],
            cwd=proj_dir,
            check=True
        )


def flash():
    # Resolve firmware paths
    for name, info in BUILDS.items():
        proj_dir = os.path.join(REPO_DIR, info["dir"])
        fw = find_firmware(proj_dir, info["env"])
        if fw is None:
            print(f"ERROR: No firmware found for {name}. Build first.")
            sys.exit(1)
        info["firmware"] = fw
        print(f"  {name:12s} -> {os.path.basename(fw)}")

    # Find bootloader and partitions from selector build
    sel_dir = os.path.join(REPO_DIR, "selector")
    sel_env = BUILDS["selector"]["env"]
    bootloader = find_build_artifact(sel_dir, sel_env, "bootloader.bin")
    partitions = find_build_artifact(sel_dir, sel_env, "partitions.bin")

    if not bootloader:
        print("ERROR: bootloader.bin not found in selector build.")
        sys.exit(1)
    if not partitions:
        print("ERROR: partitions.bin not found in selector build.")
        sys.exit(1)

    cmd_base = [sys.executable, "-m", "esptool",
                "--chip", "esp32s3", "--port", PORT, "--baud", BAUD]

    # Optional erase
    if ERASE:
        print(f"\n{'='*60}")
        print("  Erasing flash...")
        print(f"{'='*60}")
        subprocess.run(cmd_base + ["erase_flash"], check=True)

    # Flash all images
    print(f"\n{'='*60}")
    print(f"  Flashing to {PORT} at {BAUD} baud")
    print(f"{'='*60}")

    write_cmd = cmd_base + [
        "write_flash",
        ADDR_BOOTLOADER, bootloader,
        ADDR_PARTITIONS, partitions,
        ADDR_FACTORY,    BUILDS["selector"]["firmware"],
        ADDR_OTA_0,      BUILDS["meshcore"]["firmware"],
        ADDR_OTA_1,      BUILDS["meshtastic"]["firmware"],
    ]

    print(f"  {ADDR_BOOTLOADER:10s} -> bootloader.bin")
    print(f"  {ADDR_PARTITIONS:10s} -> partitions.bin")
    print(f"  {ADDR_FACTORY:10s}    -> selector")
    print(f"  {ADDR_OTA_0:10s}  -> meshcore")
    print(f"  {ADDR_OTA_1:10s}  -> meshtastic")

    subprocess.run(write_cmd, check=True)
    print("\nDone! Power-cycle to enter the boot selector.")


if __name__ == "__main__":
    if not SKIP_BUILD:
        build_all()
    flash()
