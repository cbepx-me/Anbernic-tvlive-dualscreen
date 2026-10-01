#!/bin/bash
set -e

echo "=== TVLive C++ Builder ==="

# Check dependencies
if ! pkg-config --exists sdl2; then
    echo "Error: SDL2 development files not found."
    echo "Install: sudo apt install -y build-essential cmake pkg-config libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libsdl2-gfx-dev libcurl4-openssl-dev"
    exit 1
fi

# Create build directory
mkdir -p build
cd build

# Configure and build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j2

echo ""
echo "=== Build complete ==="
echo "Binary: build/hud"
echo ""
echo "Next steps:"
echo "  1. Copy 'hud' and 'lang/i18n.json' to your device"
echo "  2. Replace the Python 'spy.py' hook in RA_launch.sh with:"
echo "     /path/to/hud spy \"\${GUIDES_ARR[@]}\" --rom \"\$ROMFILE\" &"