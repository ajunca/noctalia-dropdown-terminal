{ config, lib, pkgs, ... }:

let
  cfg = config.programs.dropdown-terminal;
  plugin = pkgs.callPackage ./default.nix {
    inherit (cfg) widthPercent heightPercent fontFamily fontSize;
  };
in
{
  options.programs.dropdown-terminal = {
    enable = lib.mkEnableOption "dropdown terminal plugin for noctalia-shell";

    widthPercent = lib.mkOption {
      type = lib.types.str;
      default = "0.6";
      description = "Panel width as a fraction of screen width (e.g. \"0.6\" for 60%).";
    };

    heightPercent = lib.mkOption {
      type = lib.types.str;
      default = "0.3";
      description = "Panel height as a fraction of screen height (e.g. \"0.3\" for 30%).";
    };

    fontFamily = lib.mkOption {
      type = lib.types.str;
      default = "Hack";
      description = "Terminal font family.";
    };

    fontSize = lib.mkOption {
      type = lib.types.str;
      default = "10.5";
      description = "Terminal font size in points.";
    };
  };

  config = lib.mkIf cfg.enable {
    xdg.configFile."noctalia/plugins/dropdown-terminal".source = plugin;
  };
}
