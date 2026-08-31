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
          hideOnFocusLoss = true;
        }
      '';
      description = ''
        Settings written to {file}`$XDG_CONFIG_HOME/dropterm/dropterm.conf`.

        Read with QSettings in INI format, so the keys live in the `[General]`
        section this module generates. Recognised keys: `widthPercent`,
        `heightPercent`, `fontFamily`, `fontSize`, `shellProgram`,
        `hideOnFocusLoss`, `backgroundOpacity`, `cornerRadius`. Anything
        omitted falls back to the built-in default.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    home.packages = [ cfg.package ];

    # QSettings resolves un-grouped keys against [General]; writing the header
    # explicitly keeps the file unambiguous rather than relying on that.
    xdg.configFile."dropterm/dropterm.conf" = lib.mkIf (cfg.settings != { }) {
      text = lib.generators.toINI { } { General = cfg.settings; };
    };
  };
}
