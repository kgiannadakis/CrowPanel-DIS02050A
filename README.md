<p align="center">
  <h1 align="center">CrowPanel DIS02050A<br>Dual-Boot LoRa Mesh Firmware</h1>
  <p align="center">
    Run <b>MeshCore</b> or <b>Meshtastic</b> on your CrowPanel — switch at boot, no reflashing.
  </p>
  <p align="center">
    <img src="https://img.shields.io/badge/ESP32--S3-LoRa_Mesh-blue?style=flat-square" alt="ESP32-S3">
    <img src="https://img.shields.io/badge/display-7%22_800x480-green?style=flat-square" alt="Display">
    <img src="https://img.shields.io/badge/radio-SX1262-orange?style=flat-square" alt="SX1262">
    <img src="https://img.shields.io/badge/license-GPL--3.0-red?style=flat-square" alt="License">
    <img src="https://img.shields.io/github/v/release/kgiannadakis/CrowPanel-DIS02050A?style=flat-square&label=firmware" alt="Release">
  </p>
</p>

---

## Overview

This project turns the **Elecrow CrowPanel 7.0"** into a standalone LoRa mesh communicator with a full touchscreen UI. A boot selector lets you choose which firmware to run at startup — no cables, no reflashing.

| Firmware | Description |
|----------|-------------|
| **MeshCore** | Feature-rich mesh chat with dark-themed LVGL UI, Telegram bridge, web dashboard, and OTA updates |
| **Meshtastic** | The popular open-source LoRa mesh platform, ported to the CrowPanel display |
| **Boot Selector** | Touchscreen menu at startup to pick your firmware |

---

## MeshCore Features

| Feature | Details |
|---------|---------|
| **Chat UI** | LVGL 8.3 dark theme, portrait & landscape, Greek/English keyboards |
| **Private Messages** | Per-message delivery tracking with automatic retries |
| **Channels** | Group messaging with receipt confirmation |
| **Telegram Bridge** | Channels to group topics, PMs to private bot chat, bidirectional |
| **Web Dashboard** | Browser-based monitoring and messaging over WiFi |
| **OTA Updates** | Over-the-air firmware updates from GitHub Releases |
| **Contacts & Repeaters** | Full contact management with signal routing |
| **WiFi + NTP** | Time sync and connectivity for bridge features |

---

## Hardware

| Component | Specification |
|-----------|--------------|
| **Board** | Elecrow CrowPanel 7.0" (DIS02050A / DIS05020A) |
| **MCU** | ESP32-S3 (8MB Flash, PSRAM) |
| **Display** | 7" 800x480 IPS, capacitive touch (GT911) |
| **Radio** | SX1262 LoRa transceiver |
| **Connectivity** | WiFi + Bluetooth (built-in) |

---

## Repository Structure

```
CrowPanel-DIS02050A/
├── meshcore/        MeshCore firmware (PlatformIO project)
├── meshtastic/      Meshtastic firmware (PlatformIO project)
├── selector/        Boot selector firmware (PlatformIO project)
├── flash_all.py     Build & flash all three in one step
└── LICENSE          GPL-3.0
```

---

## Quick Start

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- Python 3 (included with PlatformIO)
- USB-C cable

### Flash Everything

```bash
git clone https://github.com/kgiannadakis/CrowPanel-DIS02050A.git
cd CrowPanel-DIS02050A
python flash_all.py COM20              # Windows
python flash_all.py /dev/ttyUSB0       # Linux
python flash_all.py /dev/cu.usbserial  # macOS
```

This builds all three firmwares and flashes them to the correct partition addresses. Use `--skip-build` to flash without rebuilding.

### Build Individual Projects

```bash
pio run -d selector  -e boot_selector
pio run -d meshcore  -e crowpanel_v11_lvgl_chat
pio run -d meshtastic -e crowpanel-dis05020a-v11
```

---

## Partition Layout

| Partition | Address | Size | Contents |
|-----------|---------|------|----------|
| `nvs` | 0x9000 | 20 KB | Settings & preferences |
| `factory` | 0x10000 | 1 MB | Boot selector |
| `ota_0` | 0x110000 | 5.4 MB | MeshCore |
| `ota_1` | 0x680000 | 5.4 MB | Meshtastic |
| `spiffs` | 0xBF0000 | 4 MB | Chat logs & data |

---

## OTA Updates

MeshCore supports over-the-air firmware updates:

1. Connect to WiFi on the CrowPanel (**Web Apps** screen)
2. In the OTA section, enter: `kgiannadakis/CrowPanel-DIS02050A`
3. Tap **Check for Update**

The device downloads and flashes the latest release automatically.

---

## Telegram Bridge

Bridge your mesh conversations to Telegram with organized threading:

**Setup:**
1. Create a bot via [@BotFather](https://t.me/BotFather)
2. Create a Telegram group with Topics enabled, add the bot as admin
3. Enter bot token and group chat ID on the CrowPanel (**Web Apps** screen)
4. Send `/start` to the bot in a private message to link PMs

**How it works:**
- Each mesh **channel** gets its own topic thread in the Telegram group
- **PMs** go to your private chat with the bot (only you can see them)
- Send from Telegram: `/pm ContactName message` or `/ch ChannelName message`

---

## Acknowledgments

- [Meshtastic](https://meshtastic.org/) — Open-source LoRa mesh networking
- [MeshCore](https://github.com/rmendes76/MeshCore) — LoRa mesh chat framework
- [Elecrow](https://www.elecrow.com/) — CrowPanel hardware
- [LVGL](https://lvgl.io/) — Embedded graphics library

---

## License

This project is licensed under the **GNU General Public License v3.0** — see [LICENSE](LICENSE).

---

## Contributing

Contributions are welcome! Feel free to open issues or pull requests.

If you're adapting this for a different CrowPanel model, the key files to modify are the display driver, pin definitions, and partition table.
