# Khala

A fast, lightweight application launcher and file finder for Linux, built with C++ and X11.

## Features

- **File search**: Default mode - search for files and directories
- **App search**: Prefix with `!` to search for applications only
- **Command mode**: Prefix with `>` to access utility commands
- **Custom commands**: Define your own utility commands and actions
- **Responsive design**: Scales with your screen size for optimal viewing

## Installation

### Prerequisites

- C++ compiler with C++20 support
- CMake 3.10+
- X11 development libraries
- Cairo and Pango development libraries
- GTK development libraries (for clipboard support)

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
- **Enter**: Execute selected action
- **Escape**: Close launcher
- **Left/Right**: Move cursor in search field
- **Home/End**: Jump to beginning/end of search field
- **Backspace**: Delete characters

## Configuration

Khala creates a configuration file at `~/.khala/config.ini` and populates it with the default values for all available settings. Feel free to modify.

You can define custom commands by creating `.ini` files in `~/.khala/commands/`.

Each command file should follow this format:

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
```

#### Example File Action:

```ini
title=Remove
description=Open this file in Visual Studio Code
shell_cmd=rm "$FILEPATH"
is_file_action=true
```

#### Example Global Action:

```ini
title=System Monitor
description=Open system monitor
shell_cmd=htop
is_file_action=false
```

## Requirements

- Linux system with X11
