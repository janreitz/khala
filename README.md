# Khala

A lightweight application launcher and file finder for Linux and X11.

- **File search**: Default mode - search for files and directories
- **App search**: Prefix with `!` to search for applications only
- **Command mode**: Prefix with `>` to access utility commands
- **Custom commands**: Define your own utility commands and file actions
- **Responsive design**: Scales with your screen size

### Prerequisites

- C++ compiler with C++23 support (GCC 13+, Clang 14+)
- CMake 3.20+
- X11 development libraries (libX11, libXrandr (extension for multi-monitor setups))
- Cairo development libraries
- Pango development libraries
- Intel TBB (Threading Building Blocks) for parallel ranking
- pkg-config

**Ubuntu/Debian:**
```bash
sudo apt install build-essential cmake libx11-dev libxrandr-dev libcairo2-dev libpango1.0-dev libtbb-dev pkg-config
```

**Fedora/RHEL/CentOS:**
```bash
sudo dnf install gcc-c++ cmake libX11-devel libXrandr-devel cairo-devel pango-devel tbb-devel pkgconfig
```

### Build from source

```bash
git clone <repository-url>
cd khala
cmake -S . -B build
cmake --build build
```

## Usage

Set a global hotkey to the `./launcher` binary.

- **Arrow keys**: Navigate through results
- **Tab**: Open context menu for additional actions
- **Left**: Close context menu
- **Enter**: Execute selected action
- **Escape**: Close launcher

## Configuration

Khala creates a configuration file at `~/.khala/config.ini` and populates it with the default values for all available settings. Feel free to modify.

### Custom Commands

You can define custom commands by creating `.ini` files in `~/.khala/commands/`. Each command file should follow this format:

```ini
title=My Custom Action # The name shown in search results
description=Description of what this does
# Custom commands are executed in the shell
# You can access these environment variables
# FILEPATH
# FILENAME
# PARENT_DIR
shell_cmd=echo "Hello World"
# `true`: Command appears as action in file context menus
# `false`: Command appears as action in command search
is_file_action=false
# `true`: Capture command stdout and copy to clipboard
# `false`: Run command normally (default)
stdout_to_clipboard=false
```

#### Examples

```ini
# ~/.khala/commands/rm.ini
title=Remove
description=Delete File
shell_cmd=rm "$FILEPATH"
is_file_action=true
```

```ini
# ~/.khala/commands/generate_password.ini
title=Generate Password
description=Generate 16-character password and copy to clipboard
shell_cmd=openssl rand -base64 16 | tr -d '\n'
is_file_action=false
stdout_to_clipboard=true
```

There are more examples in `commands` that will be installed to the default data dir (`/usr/local/share/khala`). You can change the install location via cmake, for example `-DCMAKE_INSTALL_PREFIX=$HOME/.local`.

### Themes

You can customize the appearance by selecting one of the default themes (`default-light`, `default-dark`, `tomorrow-night-eighties`, `gruvbox-dark`, `nord`, `solarized-light`).

You can create your own themes by placing `.ini` files in:
- System-wide: `${PREFIX}/share/khala/themes/`
- User-specific: `~/.khala/themes/` (User themes override system themes with the same name.)

```ini
# My Custom Theme
input_background_color=#RRGGBB
background_color=#RRGGBB
border_color=#RRGGBB
text_color=#RRGGBB
selection_color=#RRGGBB
selection_text_color=#RRGGBB
description_color=#RRGGBB
selection_description_color=#RRGGBB
```

Colors can be specified in hex format:
- `#RGB` - Short form (e.g., `#F00` for red)
- `#RGBA` - Short form with alpha
- `#RRGGBB` - Standard form (e.g., `#FF0000` for red)
- `#RRGGBBAA` - Standard form with alpha
