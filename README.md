# TVLive DualScreen

![Version](https://img.shields.io/badge/version-1.0.1-blue.svg)
![Platform](https://img.shields.io/b/platform-Anbernic%20Dual%20Screen-green.svg)
![License](https://img.shields.io/b/license-MIT-yellow.svg)

**TVLive DualScreen** is a live TV player designed for Anbernic dual-screen handheld devices (e.g., RGds, RGdsplus). It uses the **lower screen** as the control UI and the **upper screen** for fullscreen video playback.

## Screenhost

<img width="682" height="512" alt="screenshot_upper_20260908_093505" src="https://github.com/user-attachments/assets/9c923f1e-6d48-4237-bb4d-0aa557f35f79" />
<img width="682" height="512" alt="screenshot_lower_20260908_093505" src="https://github.com/user-attachments/assets/5e44de0b-17f8-4157-bc07-270d74938569" />

## Features

- Dual-screen support (UI on lower, video on upper)
- Hardware button mapping (A/B/X/Y, L1/R1, D-Pad, Volume, Menu)
- M3U/M3U8 playlist parsing with caching
- Multi-language support (English, Chinese, Japanese, Korean, etc.)
- WiFi status, battery level, and time display
- Volume control with on‑screen progress bar
- Resume playback from last channel
- IPC volume control for mpv
- Automatic screen timeout (dim UI after inactivity)

## Requirements

- Anbernic device with dual displays (RG35xx H/Plus, RGds, etc.)
- Linux OS (stock firmware)
- Python 3.8+
- Dependencies:
  - `mpv` (for video playback)
  - `SDL2` (via `python-sdl2`)
  - `Pillow` (PIL)
  - `amixer` (for volume control)

## Installation

1. Clone the repository:
   ```bash
   git clone https://github.com/yourusername/tvlive-dualscreen.git
   cd tvlive-dualscreen
   ```

2. Install system dependencies (on device):
   ```bash
   sudo apt update
   sudo apt install mpv python3-pip
   pip3 install pillow pysdl2
   ```

3. Prepare directories:
   - Place your `.m3u` / `.m3u8` playlist files in any of the following folders:
     - `/roms/TV/`
     - `/mnt/mmc/TV/`
     - `/mnt/sdcard/TV/`
     - `./TV/` (inside the app directory)

4. (Optional) Place a custom font at `font/font.ttf`; otherwise the fallback default font is used.

## Usage

- **Launch** the program:
  ```bash
  python3 tv.py
  ```

- **Controls**:
  - **D-Pad Up/Down** – change channel
  - **D-Pad Left/Right** – page up/down
  - **L1/R1** – switch playlist source
  - **A** – play selected channel
  - **B** – stop playback
  - **X** – toggle screen off/on (UI dimming)
  - **Y** – refresh sources
  - **SELECT** – cycle languages
  - **MENU** – exit program
  - **Vol+/Vol-** – adjust volume

- **Configuration**:
  - Settings are stored in `tv.ini` (language, volume, resume position).
  - Language can also be set via `language.ini` in `/mnt/vendor/oem/` (system language file).

## File Structure

```
.
├── tv.py                # Main application
├── deps/                # (auto-created) third-party libs
├── font/                # Custom font file (optional)
├── lang/                # Language JSON files (en_US.json, zh_CN.json, ...)
├── TV/                  # Default playlist folder
├── tv.ini               # Configuration (auto-generated)
└── .cache_*.dat         # Cached playlists (auto-generated)
```

## Building / Packaging

You can create a standalone distribution using `pyinstaller`:
```bash
pip install pyinstaller
pyinstaller --onefile --add-data "lang:lang" --add-data "font:font" tv.py
```
Then copy the resulting binary and required assets to your device.

## Contributing

Please read [CONTRIBUTING.md](CONTRIBUTING.md) for details on our code of conduct and the process for submitting pull requests.

## License

This project is licensed under the MIT License – see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Inspired by the Anbernic community's love for portable media.
- Uses `mpv` for reliable video decoding.
- Built with `SDL2` and `Pillow` for lightweight UI rendering.
