# dropterm

A Yakuake-style dropdown terminal for Wayland, running on its own
`wlr-layer-shell` surface.

![screenshot](screenshot.png)

> **Coming from the noctalia plugin?** Versions up to and including `v1.1.0`
> were a QML panel plugin for [noctalia-shell](https://github.com/noctalia-dev/noctalia)
> v4. That still works — stay on the **`v1.1.0`** tag or the **`noctalia-v4`**
> branch. See [Why it left noctalia](#why-it-left-noctalia).

## Features

- Tabbed terminal sessions, persisting across open/close
- Interactive scrollbar with drag and click-to-jump
- Built on libvterm-neovim for complete terminal emulation
- Drops down flush beneath a bar without hardcoding the bar's height
- Click outside to dismiss; no shell or compositor plugin required

## How it works

The terminal renderer (`TextRender`, `VTermBridge`, `PtyIFace`) is unchanged
from the plugin. What changed is who owns the window: instead of being drawn
into a host shell's panel slot, `dropterm` creates its own layer-shell surface
via [LayerShellQt](https://invent.kde.org/plasma/layer-shell-qt) — the same
mechanism bars, docks and notification daemons use.

Two details are load-bearing:

- **The surface spans the whole usable output, not just the terminal.** A
  surface sized to the terminal alone is never told about clicks that land
  elsewhere, so outside-click dismissal would be impossible. The area around
  the terminal is transparent and simply catches those clicks. Anchoring all
  four edges with an exclusive zone of `0` makes the compositor size the
  surface to the output *minus* everyone else's exclusive zones, which is why
  the terminal sits flush under a bar with no configured offset.
- **Keyboard interactivity is exclusive.** Under focus-follows-mouse, an
  on-demand surface loses the keyboard the moment the pointer drifts off it,
  so typing would silently go elsewhere. Exclusive keeps input here while the
  terminal is mapped. Compositor keybinds still take priority, so the toggle
  works.

The consequence is that the dropdown is **modal**: while it is open, clicks
elsewhere dismiss it rather than interacting, and typing goes to the terminal.
That is the trade-off for outside-click dismissal, and it matches how shell
panels normally behave.

## Installation

**Dependencies**: Qt 6 (qtbase, qtdeclarative, qtwayland), LayerShellQt,
libvterm-neovim, CMake, pkg-config.

### NixOS (flake + home-manager)

```nix
inputs.dropterm.url = "github:ajunca/noctalia-dropdown-terminal";
```

```nix
imports = [ inputs.dropterm.homeManagerModules.default ];

programs.dropterm = {
  enable = true;
  settings = {
    widthPercent = 0.6;
    heightPercent = 0.3;
    fontFamily = "Hack";
    fontSize = 10.5;
  };
};
```

No QML import path wiring is needed any more — the QML is compiled into the
binary.

### Manual (non-Nix)

```bash
cmake -S src -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
cmake --install build
```

## Usage

`dropterm` is a single binary. The first invocation starts it; later
invocations are remote controls over a per-user socket in `$XDG_RUNTIME_DIR`.

```bash
dropterm toggle   # default
dropterm show
dropterm hide
```

Bind the toggle in your compositor. Hyprland:

```
bind = , F12, exec, dropterm toggle
```

## Configuration

Settings live in `$XDG_CONFIG_HOME/dropterm/dropterm.conf` (INI, read with
QSettings, so keys go under `[General]`). The Nix module writes this for you.

| Key | Default | Meaning |
|---|---|---|
| `widthPercent` | `0.6` | Terminal width as a fraction of the usable area (0.2–1.0) |
| `heightPercent` | `0.3` | Terminal height as a fraction of the usable area (0.15–1.0) |
| `fontFamily` | `Hack` | Terminal font |
| `fontSize` | `10.5` | Point size |
| `shellProgram` | *(empty)* | Shell to run; empty uses the shell from `passwd` |
| `backgroundOpacity` | `0.92` | Background alpha (0.0–1.0) |
| `cornerRadius` | `8` | Radius of the two free (bottom) corners |

Background blur is a compositor rule, not an application setting. Hyprland:

```
layerrule = blur, dropterm
```

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

## Why it left noctalia

noctalia v5 is a ground-up rewrite from QML/Quickshell to native C++ with a
sandboxed Luau plugin API. A terminal cannot be built on it:

1. **No PTY.** Nothing in the codebase opens one; plugins get line-streamed
   process output only. That rules out job control, interactive programs,
   terminal resize and signal delivery.
2. **No canvas.** Plugin UI is fixed declarative primitives, with no
   custom-drawing surface for a cell grid.
3. **No raw keyboard.** `capture_keys` delivers only key chords declared up
   front in the manifest, and cannot conditionally consume them. A terminal
   needs every keystroke.
4. **No native code loading.** Plugins are Luau only, so the existing C++
   terminal could not be loaded even if the above were solved.

Embedding an external terminal in a panel is not a way out either: Wayland has
no cross-client surface embedding.

Standalone sidesteps all of it, and the terminal no longer depends on any
particular shell.

## License

MIT
