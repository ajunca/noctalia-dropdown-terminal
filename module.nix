{ config, lib, pkgs, ... }:

let
  cfg = config.programs.dropterm;
in
{
  options.programs.dropterm = {
    enable = lib.mkEnableOption "dropterm, a standalone dropdown terminal";

    package = lib.mkOption {
      type = lib.types.package;
      default = pkgs.callPackage ./default.nix { };
      defaultText = lib.literalExpression "pkgs.callPackage ./default.nix { }";
      description = "The dropterm package to use.";
    };

    settings = lib.mkOption {
      type = with lib.types; attrsOf (oneOf [ bool int float str ]);
      default = { };
      example = lib.literalExpression ''
        {
          widthPercent = 0.6;
          heightPercent = 0.3;
          fontFamily = "Hack";
          fontSize = 10.5;
          foreground = "#ebebeb";
          background = "#000000";
          backgroundOpacity = 0.92;
          animationMs = 180;
        }
      '';
      description = ''
        Baseline settings, written to
        {file}`$XDG_CONFIG_HOME/dropterm/defaults.conf`.

        This is the *baseline*, not the live configuration. dropterm resolves
        each value as user override, then this file, then its built-in default,
        and only ever writes {file}`dropterm.conf` itself. So these values act
        as your declared starting point while the settings window stays usable,
        and its "Reset" returns the terminal to exactly what is declared here.

        Writing {file}`dropterm.conf` from Nix instead would make it a read-only
        symlink into the store and the settings window could never save.

        Recognised keys, with their built-in defaults:

        - `widthPercent` (0.6) and `heightPercent` (0.3) — fractions of the
          usable area, clamped to 0.2–1.0 and 0.15–1.0
        - `fontFamily` ("Hack") and `fontSize` (10.5)
        - `foreground` ("#ebebeb") and `background` ("#000000")
        - `backgroundOpacity` (0.92)
        - `cornerRadius` (8) — the two free, lower corners
        - `animationMs` (180) — roll-down duration
        - `shellProgram` ("") — empty uses the shell from passwd

        Values are read with QSettings in INI format, so they live under the
        `[General]` section this module generates.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    home.packages = [ cfg.package ];

    # QSettings resolves un-grouped keys against [General]; writing the header
    # explicitly keeps the file unambiguous rather than relying on that.
    xdg.configFile."dropterm/defaults.conf" = lib.mkIf (cfg.settings != { }) {
      text = lib.generators.toINI { } { General = cfg.settings; };
    };
  };
}
