# CrowPanel DIS02050A — Dual-Boot LoRa Mesh Firmware

A dual-boot system for the **Elecrow CrowPanel 7.0" (DIS02050A)** with ESP32-S3 and SX1262 LoRa radio. Choose between two mesh networking firmwares at boot — no reflashing needed.

## What Is This?

This project turns the CrowPanel into a standalone LoRa mesh communicator with a full touchscreen UI. At startup, a boot selector lets you pick which firmware to run:

- **MeshCore** — Feature-rich mesh chat with LVGL touchscreen UI, Telegram bridge, web dashboard, and OTA updates
- **Meshtastic** — The popular open-source LoRa mesh platform, ported to the CrowPanel's display

Both firmwares share the same LoRa hardware and can communicate with other MeshCore or Meshtastic nodes in range.

## Features

### MeshCore (v1.1.4)
- Full touchscreen chat UI (LVGL 8.3, dark theme, portrait/landscape)
- Private messages with per-message delivery tracking and retries
- Channel (group) messaging with receipt confirmation
- Contact and repeater management
- Telegram bridge — channels forwarded to a Telegram group with forum topics, PMs to private bot chat
- Web dashboard — browser-based monitoring and messaging over WiFi
- OTA firmware updates from GitHub Releases
- WiFi + NTP time sync
- Greek and English keyboard layouts

### Meshtastic
- Full Meshtastic protocol support
- Adapted for the CrowPanel's 800x480 display with LovyanGFX driver
- Compatible with all standard Meshtastic clients and nodes

### Boot Selector
- Runs from the factory partition
- Simple touchscreen menu to choose MeshCore or Meshtastic
- Remembers your last choice
- No reflashing required to switch between firmwares

## Hardware

- **Board:** Elecrow CrowPanel 7.0" (DIS02050A / DIS05020A)
- **MCU:** ESP32-S3 (8MB Flash, PSRAM)
- **Display:** 7" 800x480 capacitive touch (GT911)
- **Radio:** SX1262 LoRa transceiver
- **Connectivity:** WiFi, Bluetooth (ESP32-S3 built-in)

## Repository Structure

```
CrowPanel-DIS02050A/
├── meshcore/       — MeshCore firmware (PlatformIO project)
├── meshtastic/     — Meshtastic firmware (PlatformIO project)
├── selector/       — Boot selector firmware (PlatformIO project)
├── flash_all.py    — Build & flash all three in one step
└── LICENSE         — GPL-3.0
```

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- Python 3 (included with PlatformIO)
- USB-C cable connected to the CrowPanel

### Build & Flash

Flash all three firmwares in one command:

```bash
python flash_all.py COM20          # Windows
python flash_all.py /dev/ttyUSB0   # Linux
python flash_all.py /dev/cu.usbserial-*  # macOS
```

This builds the boot selector, MeshCore, and Meshtastic, then flashes them to the correct partition addresses.

To flash without rebuilding:

```bash
python flash_all.py COM20 --skip-build
```

### Build Individual Projects

```bash
pio run -d selector -e boot_selector
pio run -d meshcore -e crowpanel_v11_lvgl_chat
pio run -d meshtastic -e crowpanel-dis05020a-v11
```

## Partition Layout

| Partition | Address    | Size   | Contents          |
|-----------|------------|--------|-------------------|
| factory   | 0x10000    | 1 MB   | Boot selector     |
| ota_0     | 0x110000   | 5.4 MB | MeshCore          |
| ota_1     | 0x680000   | 5.4 MB | Meshtastic        |
| spiffs    | 0xBF0000   | 4 MB   | Chat logs & data  |
| nvs       | 0x9000     | 20 KB  | Settings          |

## OTA Updates (MeshCore)

MeshCore supports over-the-air firmware updates from GitHub Releases:

1. Connect to WiFi on the CrowPanel (Web Apps screen)
2. In the OTA section, enter this repo: `YOUR_USERNAME/CrowPanel-DIS02050A`
3. Tap **Check for Update**

The device downloads and flashes the new firmware automatically.

## Telegram Bridge (MeshCore)

Bridge your mesh conversations to Telegram:

1. Create a bot via [@BotFather](https://t.me/BotFather) on Telegram
2. Create a group, enable Topics, add the bot as admin with "Manage Topics" permission
3. Enter the bot token and group chat ID on the CrowPanel (Web Apps screen)
4. Send `/start` to the bot in a private message to receive PMs

Channel messages go to the group (one topic per channel). PMs go to your private bot chat.

Send messages from Telegram to the mesh:
- `/pm ContactName your message` — send a private message
- `/ch ChannelName your message` — send to a channel

## License

This project is licensed under the **GNU General Public License v3.0** — see [LICENSE](LICENSE).

Meshtastic is developed by the [Meshtastic project](https://meshtastic.org/) under GPL-3.0.
MeshCore is developed by the [MeshCore project](https://github.com/rmendes76/MeshCore).

## Contributing

Contributions are welcome! Feel free to open issues or pull requests.

If you're adapting this for a different CrowPanel model, the key files to modify are the display driver (`LovyanGFX_Driver.h`), pin definitions, and partition table.
