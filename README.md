# Launcher - Fast X11 Application Launcher

Listary for Linux

## Features (Planned)
- Instant popup with global hotkey
- Fuzzy search for applications
- Window switching
- File search
- Extensible via plugins

## Current Status
Basic X11 window with override-redirect (draws on top of everything).

## Dependencies

### Ubuntu/Debian
```bash
sudo apt-get install build-essential cmake libx11-dev
```

### Fedora
```bash
sudo dnf install gcc-c++ cmake libX11-devel
```

### Arch
```bash
sudo pacman -S base-devel cmake libx11
```

## Building

```bash
mkdir build
cd build
cmake ..
make
```

## Running

```bash
./launcher
```

Press ESC to close the window.

## Architecture Notes

- **X11**: Using raw Xlib for maximum control and speed
- **Override-redirect**: Window bypasses window manager for instant popup
- **C++20**: Modern C++ for clean, fast code
- **Future**: Will add Cairo/Pango or alternative for proper text rendering

## Next Steps
1. Add proper text rendering (Cairo/Pango vs alternatives)
2. Implement text input buffer
3. Add application indexing
4. Fuzzy search algorithm
5. Global hotkey binding
6. Configuration system
