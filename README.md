# CrossPoint Reader BLE

Firmware for the **Xteink X4** e-paper display reader (unaffiliated with Xteink).
Built using **PlatformIO** and targeting the **ESP32-C3** microcontroller.

This is a fork of [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)
with added **Bluetooth HID page-turner support**, **mini-games**, and the **Lyra UI theme**. It aims to be a
fully-featured, open-source replacement firmware with additional quality-of-life features for reading on the go.

![](./docs/images/cover.jpg)

## Release Highlights

### v1.1.1.1-ble (latest)

- Fixes a BLE input bug where long-press usage could unintentionally update learned key mappings.
- Learned BLE mappings are now only captured in **Settings → Bluetooth → Learn Page-Turn Keys**.
- Known remote profiles (GameBrick, Mini_Keyboard) are no longer overridden by learned mappings.
- Adds proper Bluetooth long-press behavior for chapter skip, matching side-button long-press behavior.
- Keeps side keys working consistently whether a Bluetooth remote is connected or not.

## What's different in this fork

| Feature | Original CrossPoint | This fork |
|---|---|---|
| Bluetooth HID page-turners | ❌ | ✅ GameBrick, Mini_Keyboard, unknown remotes |
| BLE key learning | ❌ | ✅ Learn any unknown HID device |
| Mini-games | ❌ | ✅ Chess, Caro, Sudoku, Minesweeper, 2048, Snake, Maze, Game of Life |
| Lyra UI theme | ✅ (upstream) | ✅ |
| EPUB image rendering | ✅ | ✅ (JPEG + PNG) |
| Everything else | ✅ | ✅ Inherited from upstream |

Games are based on implementations from [trilwu/crosspet](https://github.com/trilwu/crosspet) and
[shindakun/crosspoint-reader (deep-mines-ring)](https://github.com/shindakun/crosspoint-reader/tree/deep-mines-ring).
Full credit to those authors for their excellent work.

---

## Motivation

E-paper devices are fantastic for reading, but most commercially available readers are closed systems with limited
customisation. The **Xteink X4** is an affordable, e-paper device, however the official firmware remains closed.
CrossPoint exists partly as a fun side-project and partly to open up the ecosystem and truly unlock the device's
potential.

This fork adds wireless page-turning support and games to make the device more versatile — without sacrificing
the clean reading experience of the original.

This project is **not affiliated with Xteink**; it's built as a community project.

---

## Features & Usage

### Reading
- **EPUB parsing and rendering** (EPUB 2 and EPUB 3) with JPEG and PNG image support
- **Saved reading position**
- **File explorer** with nested folder support
- **Custom sleep screen** (with cover art)
- **WiFi book upload & OTA updates**
- **Configurable font, layout, and display options**
- **Screen rotation**
- **Multi-language support**: Read EPUBs in many languages ([see full list](./USER_GUIDE.md#supported-languages))

### Bluetooth HID Page-Turners
- Connect GameBrick controllers, Mini_Keyboard remotes, or any compatible HID device over BLE
- Learn the buttons of unknown remotes directly on-device

### Mini-Games
- **Chess** — full two-player chess on the e-ink display
- **Caro (Gomoku)** — five-in-a-row strategy game
- **Sudoku** — classic number puzzle
- **Minesweeper** — mine-clearing puzzle
- **2048** — tile-merging number game
- **Snake** — classic snake
- **Maze** — randomly generated mazes to navigate
- **Game of Life** — Conway's cellular automaton

### UI Themes
- **Lyra** — clean, modern home screen layout with cover art (default)
- **Lyra 3 Covers** — shows three recent book covers on the home screen
- **Cards** — card-based layout
- **Classic** — the original CrossPoint layout

See [the user guide](./USER_GUIDE.md) for full instructions. For project scope, see [SCOPE.md](SCOPE.md).

---

## Bluetooth HID Page-Turner Guide

### Supported Devices (Out of the Box)

| Device | Notes |
|---|---|
| **GameBrick controller** | Use slow-blink mode (not fast-flash pairing mode) |
| **Mini_Keyboard / Gugxiom 2-Key Keypad** | Plug-and-play |
| **Any HID keyboard/remote** | Generic arrow keys and Page Up/Down are auto-mapped; unknown remotes can use the Learn feature |

### Quick Start

1. Go to **Settings → Bluetooth** and toggle Bluetooth **ON**.
2. Tap **Scan** to discover nearby BLE devices.
3. Tap your device in the list to connect.
4. The GameBrick D-pad or remote buttons will now control page turns and navigation.
5. While reading, press the **Select** button and navigate to **Bluetooth** to access BLE settings in-book.

### Learning an Unknown Remote

If your remote is not recognised out of the box, you can teach the firmware which buttons to use:

1. Go to **Settings → Bluetooth → Learn Page-Turn Keys**.
2. Press the button you want to use as **Previous page** — the display will confirm it was captured (e.g. `Prev=0x4B @byte[2]`).
3. Press the button you want to use as **Next page**.
4. The mapping is saved to the SD card (`.crosspoint/ble_custom_profile.txt`) and applied automatically on future connections.

> **Note:** The firmware records which byte of the HID report the keycode comes from, so it works correctly
> even for non-standard devices. If your Prev and Next buttons appear to come from different report byte positions,
> the UI will warn you and ask you to retry with a consistent button pair.

### Known Device Profiles

- **GameBrick**: D-pad UP/LEFT = previous page, D-pad DOWN/RIGHT = next page. Spurious A/B events during D-pad
  use are suppressed automatically.
- **Mini_Keyboard (Gugxiom)**: The two keys are mapped to page up and page down.
- **Generic HID**: Standard arrow keys and Page Up/Page Down are mapped automatically.

### Technical Details

- Per-button cooldown logic prevents rapid accidental repeated presses.
- BLE client objects are **reused rather than deleted** to avoid a heap assertion on this ESP32-C3 target
  (`NimBLEDevice::deleteClient()` triggers a `heap_caps_free` failure on NimBLE-Arduino 2.3.6 and was removed entirely).
- The custom profile is stored as plain text `upKeycode,downKeycode,reportByteIndex` and is backward-compatible
  with the two-field format from earlier firmware versions.

---

## Mini-Games

Access games via the **Games** option on the home screen or main menu.

| Game | Description | Controls |
|---|---|---|
| **Chess** | Two-player chess | D-pad to move cursor, Select to pick/place piece |
| **Caro (Gomoku)** | Five in a row | D-pad to move, Select to place stone |
| **Sudoku** | Classic 9×9 puzzle | D-pad to navigate, number buttons to fill |
| **Minesweeper** | Mine-clearing puzzle | D-pad, Select to reveal, hold to flag |
| **2048** | Merge tiles to reach 2048 | D-pad directions |
| **Snake** | Classic snake game | D-pad to steer |
| **Maze** | Navigate a randomly-generated maze | D-pad to move |
| **Game of Life** | Conway's cellular automaton | Auto-runs; Select to step/pause |

Games are based on community contributions from:
- [**trilwu/crosspet**](https://github.com/trilwu/crosspet) — Chess, Caro, Sudoku, Minesweeper, 2048, Game of Life
- [**shindakun/crosspoint-reader** (deep-mines-ring)](https://github.com/shindakun/crosspoint-reader/tree/deep-mines-ring) — Maze (Deep Mines), Snake

Huge credit to both projects.

---

## UI Themes

Change the theme via **Settings → Display → Theme**.

- **Lyra** (default): Modern layout with large cover art and reading progress in the header.
- **Lyra 3 Covers**: Same as Lyra but shows three recent books in the top panel.
- **Cards**: Card-based grid layout for library browsing.
- **Classic**: The original CrossPoint home screen.

---

## Installing

### Releases (recommended)

Download the latest `firmware.bin` from the [Releases page](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases)
and flash it using the web flasher:

1. Connect your Xteink X4 via USB-C and wake/unlock the device.
2. Go to https://xteink.dve.al/ and use the **"OTA fast flash controls"** section to upload the downloaded `.bin`.

To revert to the upstream CrossPoint (without BLE), flash directly from https://xteink.dve.al/, or use the
**"Swap boot partition"** button at https://xteink.dve.al/debug.

### Manual / Development Build
See [Development](#development) below.

---

## Development

### Prerequisites

* **PlatformIO Core** (`pio`) or **VS Code + PlatformIO IDE**
* Python 3.8+
* USB-C cable for flashing the ESP32-C3
* Xteink X4

### Checking out the code

```sh
git clone --recursive https://github.com/thedrunkpenguin/crosspoint-reader-ble

# Or, if you have already cloned without --recursive:
git submodule update --init --recursive
```

### Flashing your device

Connect your Xteink X4 via USB-C and run:

```sh
pio run --target upload
```

### Build environments

| Environment | Use case | Log level |
|---|---|---|
| `default` | Development / debug | Debug (2) |
| `gh_release` | Production release | Error (0) |
| `gh_release_rc` | Release candidate | Info (1) |
| `slim` | Flash-constrained build | No serial output |

To build a specific environment:
```sh
pio run -e gh_release
```

### Debugging

Capture detailed logs from the serial port:

```sh
python3 -m pip install pyserial colorama matplotlib

# Linux
python3 scripts/debugging_monitor.py

# macOS
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```

Minor adjustments may be required for Windows.

---

## Internals

CrossPoint Reader aggressively caches data to the SD card to minimise RAM usage. The ESP32-C3 only has ~380KB of
usable RAM, so many design decisions were driven by this constraint.

### Data caching

```
.crosspoint/
├── epub_12471232/           # Each EPUB is cached to a subdirectory named epub_<hash>
│   ├── progress.bin         # Stores reading progress (chapter, page, etc.)
│   ├── cover.bmp            # Book cover image (once generated)
│   ├── book.bin             # Book metadata (title, author, spine, table of contents, etc.)
│   └── sections/            # All chapter data is stored in the sections subdirectory
│       ├── 0.bin            # Chapter data (screen count, all text layout info, etc.)
│       ├── 1.bin            #     files are named by their index in the spine
│       └── ...
│
├── ble_custom_profile.txt   # Learned BLE remote keycode mapping (if set)
└── epub_189013891/
```

Deleting the `.crosspoint` directory will clear the entire cache.

Due to the way it is currently implemented, the cache is not automatically cleared when a book is deleted,
and moving a book file will use a new cache directory, resetting the reading progress.

For more details on the internal file structures, see the [file formats document](./docs/file-formats.md).

---

## Credits & Attribution

- Based on [**crosspoint-reader/crosspoint-reader**](https://github.com/crosspoint-reader/crosspoint-reader) by
  Dave Allie and the CrossPoint community.
- Games ported from [**trilwu/crosspet**](https://github.com/trilwu/crosspet) — a fantastic fork featuring a
  virtual chicken companion, weather integration, and many mini-games.
- Maze (Deep Mines) and Snake from [**shindakun/crosspoint-reader** (deep-mines-ring)](https://github.com/shindakun/crosspoint-reader/tree/deep-mines-ring).
- Original e-reader inspiration: [diy-esp32-epub-reader by atomic14](https://github.com/atomic14/diy-esp32-epub-reader).

---

## Contributing

Contributions are very welcome!

For BLE or game-related issues/features, open an issue in this repository.
For core EPUB rendering or base firmware issues, consider contributing upstream to
[crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader).

CrossPoint Reader is **not affiliated with Xteink or any manufacturer of the X4 hardware**.
