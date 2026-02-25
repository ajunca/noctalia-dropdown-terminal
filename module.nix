{ config, lib, pkgs, ... }:

let
  cfg = config.programs.dropdown-terminal;
  plugin = pkgs.callPackage ./default.nix { };
in
{
  options.programs.dropdown-terminal = {
    enable = lib.mkEnableOption "dropdown terminal plugin for noctalia-shell";
  };

  config = lib.mkIf cfg.enable {
    xdg.configFile."noctalia/plugins/dropdown-terminal".source = plugin;
  };
}
