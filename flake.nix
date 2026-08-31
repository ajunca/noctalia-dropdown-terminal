{
  description = "dropterm — standalone Yakuake-style dropdown terminal on wlr-layer-shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
  let
    forAllSystems = nixpkgs.lib.genAttrs [ "x86_64-linux" "aarch64-linux" ];
  in
  {
    homeManagerModules.default = import ./module.nix;

    packages = forAllSystems (system: {
      default = (import nixpkgs { inherit system; }).callPackage ./default.nix {};
    });
  };
}
