# Maintainer: James <jamespo@gmail.com>
pkgname=dropterm-git
pkgver=2.0.0.r31.gab33ac8
pkgrel=1
pkgdesc="Standalone Yakuake-style dropdown terminal for wlr-layer-shell compositors"
arch=('x86_64' 'aarch64')
url="https://github.com/ajunca/noctalia-dropdown-terminal"
license=('MIT')
depends=('qt6-base' 'qt6-declarative' 'qt6-wayland' 'layer-shell-qt' 'libvterm')
makedepends=('cmake' 'pkg-config' 'git')
provides=('dropterm')
conflicts=('dropterm')
source=("$pkgname::git+$url.git")
sha256sums=('SKIP')

pkgver() {
  cd "$pkgname"
  local base
  base=$(grep -m1 -oP '(?<=VERSION )[0-9]+\.[0-9]+\.[0-9]+' src/CMakeLists.txt)
  printf '%s.r%s.g%s' "$base" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
  cmake -S "$pkgname/src" -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build build
}

package() {
  DESTDIR="$pkgdir" cmake --install build
}
