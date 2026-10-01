# TVLive for Anbernic RK3568

A high-performance live TV streaming application for Anbernic RK3568 handheld devices (RG35XX Plus series and compatible models).

## Overview

TVLive is an IPTV player designed specifically for Anbernic RK3568 handhelds. It supports M3U playlists, channel logo display, multi-language UI, and is optimized for low-memory embedded devices.

## Features

- 📺 **M3U Playlist Support** — Load standard M3U/M3U8 playlists
- 🖼️ **Channel Logos** — Async logo loading with disk cache
- 🌐 **Multi-language** — 10 languages supported (English, Chinese, Japanese, Korean, etc.)
- 📱 **Touch & Buttons** — Full touchscreen and gamepad button support
- 🔊 **Volume Control** — Via mpv IPC, works during playback
- 💾 **Resume** — Remembers last channel, source, and volume
- 🎨 **Dark / Light Theme** — Toggle anytime with L2

## Requirements

### Build Dependencies (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
    libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libsdl2-gfx-dev \
    libminizip-dev libtinyxml2-dev
```

### Runtime Dependencies (on device)

All required shared libraries are pre-installed on Anbernic RK3568 devices:

- SDL2 (2.0.12+)
- SDL2_image (2.0.9+)
- SDL2_ttf
- SDL2_gfx
- mpv (for video playback)

## Building

```bash
git clone https://github.com/cbepx-me/TVLive_for_Anbernic.git
cd TVLive_for_Anbernic
./build.sh
```

The binary will be at `build/tvlive`.

## Usage

### Playlists

Place your M3U/M3U8 files in `/mnt/mmc/TV/`, `/mnt/sdcard/TV/`, or `/roms/TV/`.

### Controls

| Button | Action |
|--------|--------|
| ↑ / ↓ | Channel up/down |
| ← / → | Page up/down |
| A | Play selected channel |
| B | Stop playback / Exit |
| L1 / R1 | Previous / Next source |
| L2 | Toggle theme |
| L3 | Cycle font size |
| SELECT | Exit |
| MENUF | Exit |
| V+ / V- | Volume up/down |

## Touch
- Tap channel list to select
- Tap buttons at bottom for actions

## Project Structure

```
src/                    — C++ source files
lang/i18n.json          — Translation strings
third_party/            — Bundled third-party headers (nlohmann/json)
CMakeLists.txt          — Build configuration
build.sh                — One-click build script
```

## Technical Notes

### GPU Driver Compatibility

The RK3568 uses a Mali G31 GPU. SDL2's `SDL_RENDERER_ACCELERATED` may fail on some firmware versions. The application automatically falls back to software rendering if hardware acceleration is unavailable.

### Cross-Compilation

The device runs Ubuntu 22.04 LTS with glibc 2.35. To build binaries compatible with the device:

- Use GCC ≤ 12 to avoid GLIBCXX version mismatches
- Link against glibc ≤ 2.35
- A native aarch64 build (inside an arm64 Docker container) produces working binaries

### mpv IPC

Volume control is implemented via mpv's JSON IPC interface. The socket path is `/tmp/tv-mpv-<pid>.sock`.

## License

MIT License

## Credits

- Original Python implementation: cbepx-me
- C++ port: cbepx-me
- SDL2, SDL2_image, SDL2_ttf, SDL2_gfx
- nlohmann/json
- minizip
- tinyxml2
```

