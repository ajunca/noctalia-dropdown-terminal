{ lib, stdenv, cmake, pkg-config, qt6, kdePackages, libvterm-neovim }:

stdenv.mkDerivation {
  pname = "dropterm";
  version = "2.0.0";

  src = ./src;

  nativeBuildInputs = [
    cmake
    pkg-config
    qt6.wrapQtAppsHook
  ];

  buildInputs = [
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtwayland          # Wayland platform plugin, required at runtime
    kdePackages.layer-shell-qt
    libvterm-neovim
  ];

  meta = {
    description = "Standalone Yakuake-style dropdown terminal on wlr-layer-shell";
    homepage = "https://github.com/ajunca/noctalia-dropdown-terminal";
    # Relicensed to MIT in ce96dd1 (the C++ was rewritten from scratch for it);
    # this expression still claimed GPL-2.0+ until 2.0.0.
    license = lib.licenses.mit;
    mainProgram = "dropterm";
    platforms = lib.platforms.linux;
  };
}
