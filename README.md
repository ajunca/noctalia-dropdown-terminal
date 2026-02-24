# noctalia-dropdown-terminal

A Yakuake-style dropdown terminal plugin for [noctalia-shell](https://github.com/noctalia-dev/noctalia-shell) on Wayland.

![screenshot](screenshot.png)

## Features

- Tabbed terminal sessions (persist across panel open/close)
- Interactive scrollbar with drag and click-to-jump
- Built on libvterm-neovim for complete terminal emulation
- Built-in settings popup (gear button) for width, height, font and size
- Settings persist across sessions via noctalia plugin API
- Configurable defaults via Nix options

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

## Installation (NixOS + home-manager)

Add as a flake input:

```nix
inputs.noctalia-dropdown-terminal.url = "github:ajunca/noctalia-dropdown-terminal";
```

Import the home-manager module and enable:

```nix
imports = [ inputs.noctalia-dropdown-terminal.homeManagerModules.default ];

programs.dropdown-terminal = {
  enable = true;
  widthPercent = "0.6";    # 60% of screen width
  heightPercent = "0.3";   # 30% of screen height
  fontFamily = "Hack";
  fontSize = "10.5";
};
```

Add the QML import path to noctalia-shell (required for the compiled plugin):

```nix
programs.noctalia-shell.package = pkgs.noctalia-shell.overrideAttrs (old: {
  postFixup = (old.postFixup or "") + ''
    wrapProgram $out/bin/noctalia-shell \
      --prefix QML_IMPORT_PATH : "${inputs.noctalia-dropdown-terminal.packages.${pkgs.stdenv.hostPlatform.system}.default}"
  '';
});
```

Bind a key to toggle the panel (e.g. in niri):

```nix
"F11".action.spawn = [ "noctalia-shell" "ipc" "call" "plugin" "togglePanel" "dropdown-terminal" ];
```

## License

GPL-2.0-or-later. Terminal rendering derived from [literm](https://github.com/rburchell/literm) by Robin Burchell / Crimson AS (itself based on fingerterm by Heikki Holstila). VT100 parser replaced with libvterm-neovim.
