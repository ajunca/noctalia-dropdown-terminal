{ lib, stdenv, cmake, pkg-config, qt6, libvterm-neovim }:

stdenv.mkDerivation {
  pname = "noctalia-dropdown-terminal";
  version = "1.1.0";

  src = ./src;

  nativeBuildInputs = [
    cmake
    pkg-config
    qt6.wrapQtAppsHook
  ];

  buildInputs = [
    qt6.qtbase
    qt6.qtdeclarative
    libvterm-neovim
  ];

  dontWrapQtApps = true;

  postInstall = ''
    mkdir -p $out
    cp ${./Panel.qml} $out/Panel.qml
    cp ${./Settings.qml} $out/Settings.qml
    cp ${./manifest.json} $out/manifest.json
    mv $out/lib/qt-6/qml/dropterm $out/dropterm
    rm -rf $out/lib
  '';

  meta = {
    description = "Yakuake-style dropdown terminal plugin for noctalia-shell";
    license = lib.licenses.gpl2Plus;
    platforms = lib.platforms.linux;
  };
}
