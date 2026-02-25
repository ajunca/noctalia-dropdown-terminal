# noctalia-dropdown-terminal

A Yakuake-style dropdown terminal plugin for [noctalia-shell](https://github.com/noctalia-dev/noctalia-shell) on Wayland.

![screenshot](screenshot.png)

## Features

- Tabbed terminal sessions (persist across panel open/close)
- Interactive scrollbar with drag and click-to-jump
- Built on libvterm-neovim for complete terminal emulation
- Configurable width, height, font and size via noctalia Settings panel
- Settings persist across sessions via noctalia plugin API

## Requirements

This plugin includes a compiled C++ QML module (`dropterm`) that provides the terminal emulator. You need to build it before use.

**Dependencies**: Qt 6 (qtbase, qtdeclarative), libvterm-neovim, CMake, pkg-config

## Installation

### NixOS (flake + home-manager)

Add as a flake input:

```nix
inputs.noctalia-dropdown-terminal.url = "github:ajunca/noctalia-dropdown-terminal";
```

Import the home-manager module and enable:

```nix
imports = [ inputs.noctalia-dropdown-terminal.homeManagerModules.default ];

programs.dropdown-terminal.enable = true;
```

Add the QML import path to noctalia-shell (required for the compiled module):

```nix
programs.noctalia-shell.package = pkgs.noctalia-shell.overrideAttrs (old: {
  postFixup = (old.postFixup or "") + ''
    wrapProgram $out/bin/noctalia-shell \
      --prefix QML_IMPORT_PATH : "${inputs.noctalia-dropdown-terminal.packages.${pkgs.stdenv.hostPlatform.system}.default}"
  '';
});
```

### Manual (non-Nix)

Build the compiled module:

```bash
cd src
cmake -B build -DCMAKE_INSTALL_PREFIX=../install
cmake --build build
cmake --install build
```

Copy files to the noctalia plugins directory:

```bash
PLUGIN_DIR=~/.config/noctalia/plugins/dropdown-terminal
mkdir -p "$PLUGIN_DIR/dropterm"
cp install/lib/qt-6/qml/dropterm/libdropterm.so "$PLUGIN_DIR/dropterm/"
cp install/lib/qt-6/qml/dropterm/qmldir "$PLUGIN_DIR/dropterm/"
cp Panel.qml Settings.qml manifest.json "$PLUGIN_DIR/"
```

Launch noctalia-shell with the plugin directory on the QML import path:

```bash
QML_IMPORT_PATH=~/.config/noctalia/plugins/dropdown-terminal:$QML_IMPORT_PATH noctalia-shell
```

## Usage

Bind a key to toggle the panel (e.g. in niri):

```nix
"F12".action.spawn = [ "noctalia-shell" "ipc" "call" "plugin" "togglePanel" "dropdown-terminal" ];
```

Or from the command line:

```bash
noctalia-shell ipc call plugin togglePanel dropdown-terminal
```

Configure width, height, font and size from the noctalia Settings panel.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| Ctrl+Shift+C | Copy |
| Ctrl+Shift+V | Paste |
| Shift+Insert | Paste |
| Ctrl+Shift+T / N | New tab |
| Ctrl+Shift+W | Close tab |
| Ctrl+Tab | Next tab |
| Ctrl+Shift+Tab | Previous tab |

## License

MIT
