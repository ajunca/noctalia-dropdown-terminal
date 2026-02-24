{ lib, stdenv, cmake, pkg-config, qt6, libvterm-neovim
, widthPercent ? "0.6"
, heightPercent ? "0.3"
, fontFamily ? "Hack"
, fontSize ? "10.5"
}:

stdenv.mkDerivation {
  pname = "noctalia-dropdown-terminal";
  version = "1.0.0";

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
    substitute ${./Panel.qml} $out/Panel.qml \
      --replace-fail "@WIDTH_PERCENT@" "${widthPercent}" \
      --replace-fail "@HEIGHT_PERCENT@" "${heightPercent}" \
      --replace-fail "@FONT_FAMILY@" "${fontFamily}" \
      --replace-fail "@FONT_SIZE@" "${fontSize}"
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
