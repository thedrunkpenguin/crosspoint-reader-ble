# CrossPoint Reader BLE

Firmware for the **Xteink X4** e-paper display reader (unaffiliated with Xteink).
Built using **PlatformIO** and targeting the **ESP32-C3** microcontroller.

This is a fork of [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)
with added **Bluetooth HID page-turner support**, **mini-games**, and the **Lyra UI theme**. It aims to be a
fully-featured, open-source replacement firmware with additional quality-of-life features for reading on the go.

![](./docs/images/cover.jpg)

**NOTE** there can be ghosting due to memory with the bluetooth enabled. Changing to 1 page refresh helps. I've also noticed moving the cursor a few times will get it to refresh. I am working on better performance. I've noticed running my epubs through the optimizer (https://epubdev.pages.dev/) gets a better performance. 
If your remote connects but buttons do not work, try custom key mapping. If it still does not work then there is a debug you can turn on and take a picture of it and upload here and I can try to get your device working. I've had confirmation the Free 3 and a few other devices work along with the gamebrick and gugxiom keypad. I will have a free 2 soon to do additional testing and hard code the Free devices to work when paired. 

## Release Highlights

### v1.1.1-ble.19 (latest)

- **Firmware downloads**:
  - Single image: [crosspoint-reader-ble-v1.1.1-ble.19-firmware.bin](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.19/crosspoint-reader-ble-v1.1.1-ble.19-firmware.bin)
  - Full flash bundle (bootloader + partitions + firmware + instructions): [crosspoint-reader-ble-v1.1.1-ble.19-full-flash.zip](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.19/crosspoint-reader-ble-v1.1.1-ble.19-full-flash.zip)
  - Checksums: [SHA256SUMS.txt](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.19/SHA256SUMS.txt)
- **Fixes the Bluetooth-connected Home panic** when returning to the main menu and recent-book previews refresh.
- **XTC / XTCH thumbnails now stay low-memory under BLE load**, so large books can regenerate Home covers without aborting.
- **Includes the recent XTC stability work** for SD-streamed paging, cover generation, and silent GitHub release builds.

### v1.1.1-ble.17

- **Firmware downloads**:
  - Single image: [crosspoint-reader-ble-v1.1.1-ble.17-firmware.bin](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.17/crosspoint-reader-ble-v1.1.1-ble.17-firmware.bin)
  - Full flash bundle (bootloader + partitions + firmware + instructions): [crosspoint-reader-ble-v1.1.1-ble.17-full-flash.zip](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.17/crosspoint-reader-ble-v1.1.1-ble.17-full-flash.zip)
  - Checksums: [SHA256SUMS.txt](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.17/SHA256SUMS.txt)
- **Free 2 stays page-turn only**: short presses now page normally again without accidental chapter skips.
- **GitHub release build is now silent**: serial logging and the BLE debug monitor are both disabled in the published firmware.
- **Large XTC / XTCH books still open reliably**, with the SD-streaming render path kept in place for big files.

### v1.1.1-ble.16

- **Firmware downloads**:
  - Single image: [crosspoint-reader-ble-v1.1.1-ble.16-firmware.bin](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.16/crosspoint-reader-ble-v1.1.1-ble.16-firmware.bin)
  - Full flash bundle (bootloader + partitions + firmware + instructions): [crosspoint-reader-ble-v1.1.1-ble.16-full-flash.zip](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.16/crosspoint-reader-ble-v1.1.1-ble.16-full-flash.zip)
  - Checksums: [SHA256SUMS.txt](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.16/SHA256SUMS.txt)
- **Large XTC / XTCH books now open reliably**: page rendering now streams directly from SD instead of allocating a full extra page bitmap in RAM, eliminating the out-of-memory error seen on large files and verified with a 100+ MB test file.
- **GameBrick chapter skip is more reliable**: long `B` holds are now merged correctly so backward chapter skip is less likely to fall through into a quick page-back.
- **Free2 long-press behavior stays isolated** so the Free2 workaround no longer changes hold timing semantics for other BLE remotes.

### v1.1.1-ble.15

- **Firmware downloads**:
  - Single image: [crosspoint-reader-ble-v1.1.1-ble.15-firmware.bin](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.15/crosspoint-reader-ble-v1.1.1-ble.15-firmware.bin)
  - Full flash bundle (bootloader + partitions + firmware + instructions): [crosspoint-reader-ble-v1.1.1-ble.15-full-flash.zip](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.15/crosspoint-reader-ble-v1.1.1-ble.15-full-flash.zip)
  - Checksums: [SHA256SUMS.txt](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.15/SHA256SUMS.txt)
- **Restored long-press chapter skip** for all physical-button remotes (GameBrick, mini_keyboard, etc.) — reverts an accidental regression from ble.14 where chapter-skip was removed from all remotes.

### v1.1.1-ble.14

- **New Subreddit Reader mode** in Network: browse one subreddit on-device with post list, body text, comments, and image view.
- **Free 2 Now working**: added support for the free 2 device.


### v1.1.1-ble.12

- **Firmware downloads**:
  - Single image: [crosspoint-reader-ble-v1.1.1-ble.12-firmware.bin](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.12/crosspoint-reader-ble-v1.1.1-ble.12-firmware.bin)
  - Full flash bundle (bootloader + partitions + firmware + instructions): [crosspoint-reader-ble-v1.1.1-ble.12-full-flash.zip](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.12/crosspoint-reader-ble-v1.1.1-ble.12-full-flash.zip)
  - Checksums: [SHA256SUMS.txt](https://github.com/thedrunkpenguin/crosspoint-reader-ble/releases/download/v1.1.1-ble.12/SHA256SUMS.txt)
- **RPG: Companions of the Lance overhaul** inspired by Dragonlance + Neverwinter tone and structure.
- **D&D 5e mechanics integrated**: proficiency bonus scaling, corrected class hit-dice progression, and full XP thresholds.
- **Combat update**: Rogue/Ranger attacks now use DEX where appropriate.
- **New enemy ladder and bosses**: kobolds, draconians, wights, drow scouts, and a young dragon capstone encounter.
- **Narrative refresh**: Inn of the Last Home, Solace, and Darken Wood replace generic location text.
- **BLE/HID stability improvements** preserved from prior versions.

### v1.1.1.10-ble

- **IINE Game Brick full D-pad support**: Up, Down, Left, Right, A and B buttons all decode correctly in both menu and reader contexts.
- Fixes **A-button → Up cross-trigger** that caused unintended upward navigation when pressing A on the Game Brick.
- Adds **multi-mode detection**: Game Brick's C/T/H firmware modes (changed by holding the mode button) are now auto-detected — the standard-HID path (mode H) is handled alongside the proprietary mode-C decode.
- Improves **LEFT reliability** with a burst-frame detector for the centered-button pattern used by some Game Brick units.
- **Pet save format versioned** (`PET1` magic + version byte) so the save survives firmware updates without resetting the pet.
- **Pet name sanitization**: names are validated on load, preventing garbled text or file-path strings from appearing as the pet's name after an unclean save.
- Adds **"Start Over"** to the Virtual Pet action menu — wipes the save file and resets the pet to a fresh egg for testing or a fresh start.
- Fixes **rename keyboard re-opening**: after confirming a new pet name the on-screen keyboard no longer immediately re-launches.
- RPG activity foundation committed (character, encounter, item stubs) — not yet accessible from the menu.

### v1.1.1.9-ble

- Adds a playable **Golf Solitaire** card game, accessible from the Games menu alongside Deep Mines.
- Solitaire layout is fully responsive — all seven card columns fit the device screen without scrolling.
- Fixes **pet card icon** on the Cards home-screen theme: icon now uses a native 48×48 asset, eliminating render artefacts.

### v1.1.1.8-ble

- **Pet persistence**: always creates the save directory before writing, fixing a silent failure that caused pets to reset on reboot when the directory did not exist.
- **Deep Mines dungeon seeds** now use `esp_random()` (hardware RNG) instead of a millis-based value, so every new run is genuinely random.
- **Games list streamlined**: Games menu now shows only Deep Mines and Solitaire; other game code is preserved for future re-addition.
- **Cards theme UI improvements**:
  - Recent books card enlarged to 60% of the right column (was 50%).
  - Virtual pet card shrunk to 40% (was 50%).
  - Book titles now word-wrap at space boundaries and show two lines instead of truncating with an ellipsis.
  - Progress bar added below each book title showing reading percentage.

### v1.1.1.7-ble

- Bluetooth Debug Monitor now shows **unique keycodes with hit counts** for better unknown-remote integration.
- Unique key list is sorted by **highest frequency first**, making dominant forward/back candidate codes easier to identify.
- Keeps existing wizard/test counters and per-device persistence behavior from `v1.1.1.6-ble`.

### v1.1.1.6-ble

- Adds a true **Remote Setup Wizard** flow: Forward → Back → 10-second test → save.
- Wizard test step now shows live **Forward/Back hit counters** so users can verify key capture.
- Wizard success now returns directly to the Bluetooth menu (or back to the book when opened in-reader).
- Adds a dedicated **Bluetooth Debug Monitor** screen (connected count, event count, last key, last event age).
- Adds **per-device learned key persistence by MAC**, auto-applied on reconnect for unknown remotes.
- Adds HID report-map driven hints (Keyboard/Consumer usage-page detection + preferred byte heuristic).
- Improves reconnect robustness (protocol mode write, stale CCCD clear, notify/indicate-aware subscribe).
- Adds central input de-jitter suppression for duplicate rapid events.

### v1.1.1.5-ble

- Adds **Simple Bluetooth fallback** for unknown remotes: for devices without a known profile,
  first detected key is treated as **Forward**, second distinct key as **Back**.
- Keeps existing known-device profiles (GameBrick, MINI_KEYBOARD, etc.) unchanged.
- Fixes intermittent idle lockups seen with Bluetooth enabled by preventing low-power CPU mode while
  BLE is active and ensuring Bluetooth shutdown runs at normal speed before deep sleep.
- Preserves existing GameBrick duplicate-back suppression improvements from `v1.1.1.4-ble`.

### v1.1.1.4-ble

- Fixes duplicate back-page triggers for **GameBrick / IINE_control** remotes where one physical press could
  generate two page-back actions (visible as a double screen blink).
- GameBrick handling now ignores transitional noise frames without forcing internal press-state resets.
- Adds GameBrick-specific press deduping so key-change chatter while a button is still held does not create
  a second synthetic press event.
- Adds a short GameBrick duplicate debounce window for repeated identical keycode injections.
- Keeps **MINI_KEYBOARD** behavior unchanged.

### v1.1.1.3-ble

Improved BLE key learning for unknown and generic devices — addresses the common case where a user could
learn the **Previous** button but the **Next** button failed to save:

- **Learn Keys no longer rejects mismatched byte positions.** Previously if your remote's Prev and Next
  buttons sent their keycodes at different offsets inside the HID report the firmware would display
  `"NEXT key must use byte[X]"` and block the save. This check has been removed — any two different
  keycodes are now accepted, making the learn flow reliable for a much wider range of hardware.
- **Custom profiles now scan the full HID report.** After learning, when a notification arrives the
  firmware searches all 8 bytes of the report for your learned codes instead of only looking at the
  single fixed byte captured during learning. This handles remotes that send Prev/Next on separate
  HID characteristics or with variable-length frames.
- **User-learned profile takes priority over fuzzy name matches.** If you have gone through Learn Keys,
  your mapping wins over any automatic name-based profile guess (e.g. a device called "Mini Remote"
  no longer silently falls back to the built-in MINI_KEYBOARD codes). MAC-address matches and the
  GameBrick strict profile still take precedence.
- **Custom profile fallback in key mapping.** Even when a non-strict built-in profile is active, if
  a keycode does not match that profile's expected codes the firmware also checks your learned mapping.
  This makes hybrid devices — where one button matches a known profile and the other does not — work
  without needing to clear the built-in profile first.
- **GameBrick protected from accidental override.** The GameBrick uses a non-standard bitmask report
  layout and is now marked `strictProfile`, so it is never replaced by a user-learned mapping.
- **1-byte Consumer Control reports now processed.** Short HID frames such as `[0xE9]` or `[0xEA]`
  (common on presentation clickers) are no longer dropped by the minimum-length check.
- **Added "Disconnect Device(s)"** option to Settings → Bluetooth main menu for a quick manual
  disconnect without leaving the settings screen.
- **Bluetooth reconnect reliability improvements.** Stale connected-device entries are now detected
  via a live `client->isConnected()` check, preventing a device appearing connected when it is not.
  Subscribe failures during reconnect are detected and retried with a fresh client object.
- **Auto-sleep uses a dedicated timer** that is not reset by BLE keepalive traffic, so the device
  sleeps after the configured idle period even when a Bluetooth remote is actively paired.

### v1.1.1.2-ble

- Version string corrected to match the release tag.
- Baseline stability from v1.1.1.1-ble preserved.

### v1.1.1.1-ble

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
- **Deep Mines** — randomly generated dungeon maze to navigate
- **Solitaire**
- **Fantasy RPG** — text based rpg

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
2. Press the button you want to use as **Previous page** — the display confirms with e.g. `Prev=0x4B`.
3. Press a **different** button for **Next page** — the display shows `Saved! Prev=0xXX Next=0xXX`.
4. The mapping is written to the SD card (`.crosspoint/ble_custom_profile.txt`) and applied automatically
   on all future connections.

> **Tip:** If only one direction works after learning, go to **Settings → Bluetooth → Clear Learned Keys**
> and repeat the process. The byte-position mismatch restriction that blocked many generic remotes in
> earlier versions has been removed in v1.1.1.3.

### Known Device Profiles

- **GameBrick (IINE Game Brick)**: D-pad UP/LEFT = previous page, D-pad DOWN/RIGHT = next page. A and B buttons work correctly as confirm/select in menus. Spurious A/B cross-triggers during D-pad use are suppressed. All three firmware modes (C/T/H, toggled by holding the mode button until the LED changes) are auto-detected.
- **Mini_Keyboard (Gugxiom)**: The two keys are mapped to page up and page down.
- **Generic HID**: Standard arrow keys and Page Up/Page Down are mapped automatically.

### Technical Details

- Per-button cooldown logic prevents rapid accidental repeated presses.
- BLE client objects are **reused rather than deleted** to avoid a heap assertion on this ESP32-C3 target
  (`NimBLEDevice::deleteClient()` triggers a `heap_caps_free` failure on NimBLE-Arduino 2.3.6).
- The custom profile is stored as plain text `upKeycode,downKeycode,reportByteIndex` on the SD card
  and is backward-compatible with the two-field format from earlier firmware versions.
- Device profile priority (highest to lowest): ① MAC-address prefix match → ② User-learned custom
  profile (for non-strict profiles) → ③ Fuzzy device-name match.
- The **GameBrick** profile is `strictProfile` — its non-standard bitmask/byte[4] report layout is
  protected from being silently overridden by a user-learned custom mapping.
- Custom profile matching scans all 8 report bytes for learned codes, so Prev/Next buttons that
  transmit their keycodes at different byte offsets or on separate HID characteristics both work.
- `isConnected()` and `getConnectedDevices()` verify that the link is actually live via
  `client->isConnected()`, preventing stale entries from blocking reconnect attempts.

---

## Mini-Games

Access games via the **Games** option on the home screen or main menu.

| Game | Description | Controls |
|---|---|---|
| **Deep Mines (Maze)** | Navigate a randomly-generated dungeon | D-pad to move |
| **Golf Solitaire** | Card game — clear all cards from the tableau | D-pad to select, Confirm to play |
| **Chess** | Two-player chess | D-pad to move cursor, Select to pick/place piece |
| **Caro (Gomoku)** | Five in a row | D-pad to move, Select to place stone |
| **Sudoku** | Classic 9×9 puzzle | D-pad to navigate, number buttons to fill |
| **Minesweeper** | Mine-clearing puzzle | D-pad, Select to reveal, hold to flag |
| **2048** | Merge tiles to reach 2048 | D-pad directions |
| **Snake** | Classic snake game | D-pad to steer |
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
├── pet/
│   └── state.bin            # Virtual pet save (versioned binary, magic PET1)
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
