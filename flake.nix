{
  description = "noctalia-dropdown-terminal — Yakuake-style dropdown terminal for noctalia-shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
  let
    forAllSystems = nixpkgs.lib.genAttrs [ "x86_64-linux" "aarch64-linux" ];
  in
  {
    homeManagerModules.default = import ./module.nix;

    packages = forAllSystems (system:
      let pkgs = nixpkgs.legacyPackages.${system};
      in {
        default = pkgs.callPackage ./default.nix {};
      }
    );
  };
}
