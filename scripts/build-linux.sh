#!/bin/bash
# Build Claude Box for Linux and produce distributable artifacts:
#   - AppImage (any distro, self-contained)
#   - .deb     (Debian/Ubuntu, if dpkg-dev is installed)
#   - .rpm     (Fedora/RHEL, if rpm-build is installed)
#
# Run from anywhere; the script locates the repo via its own path.
# Installs missing build prerequisites via apt, dnf, or pacman.
# Ubuntu 22.04+ / Fedora 36+ / Arch are the tested targets.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
GUI_DIR="$REPO_DIR/gui"
BUILD_DIR="$GUI_DIR/build"
TOOLS_DIR="$BUILD_DIR/linuxdeploy-tools"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
info() { printf '\033[1;34m==> %s\033[0m\n' "$*"; }
ok()   { printf '\033[1;32m    OK: %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33m  WARN: %s\033[0m\n' "$*"; }
die()  { printf '\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------------------
# Detect package manager and map to distro-specific package names
# ---------------------------------------------------------------------------
if have apt-get; then
    PKG_MANAGER=apt
    BUILD_PKGS=(cmake build-essential pkg-config qt6-base-dev libvterm-dev)
    installed() { dpkg -s "$1" >/dev/null 2>&1; }
    install_pkgs() { sudo apt-get install -y "$@"; }
elif have dnf; then
    PKG_MANAGER=dnf
    BUILD_PKGS=(cmake gcc-c++ make pkg-config qt6-qtbase-devel libvterm-devel)
    installed() { rpm -q "$1" >/dev/null 2>&1; }
    install_pkgs() { sudo dnf install -y "$@"; }
elif have pacman; then
    PKG_MANAGER=pacman
    BUILD_PKGS=(cmake base-devel pkgconf qt6-base libvterm)
    installed() { pacman -Q "$1" >/dev/null 2>&1; }
    install_pkgs() { sudo pacman -S --noconfirm "$@"; }
else
    die "No supported package manager found (apt-get, dnf, pacman)"
fi
ok "Package manager: $PKG_MANAGER"

# ---------------------------------------------------------------------------
# Build prerequisites
# ---------------------------------------------------------------------------
info "Checking build prerequisites"
MISSING=()
for pkg in "${BUILD_PKGS[@]}"; do
    installed "$pkg" || MISSING+=("$pkg")
done
if [[ ${#MISSING[@]} -gt 0 ]]; then
    info "Installing: ${MISSING[*]}"
    install_pkgs "${MISSING[@]}"
else
    ok "All build prerequisites present"
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
info "Configuring"
cmake -B "$BUILD_DIR" "$GUI_DIR"

info "Building"
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

# ---------------------------------------------------------------------------
# AppImage via linuxdeploy
# ---------------------------------------------------------------------------
info "Preparing AppImage"

ARCH="$(uname -m)"
mkdir -p "$TOOLS_DIR"

download_tool() {
    local name="$1"
    local url="$2"
    local dest="$TOOLS_DIR/$name"
    if [[ ! -x "$dest" ]]; then
        info "Downloading $name"
        curl -fL --progress-bar -o "$dest" "$url"
        chmod +x "$dest"
    else
        ok "$name already downloaded"
    fi
}

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-$ARCH.AppImage"
LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-$ARCH.AppImage"

download_tool "linuxdeploy-$ARCH.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage"
download_tool "linuxdeploy-plugin-qt-$ARCH.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$ARCH.AppImage"

# Stage the install tree into AppDir so linuxdeploy sees the right paths.
APPDIR="$BUILD_DIR/AppDir"
rm -rf "$APPDIR"
cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr"

# linuxdeploy needs qmake on PATH to run the Qt plugin.
QT_BIN="$(dirname "$(command -v qmake 2>/dev/null || true)")"
if [[ -z "$QT_BIN" ]]; then
    # Try pkg-config as a fallback to locate the Qt bin directory.
    QT_BIN="$(pkg-config --variable=bindir Qt6Core 2>/dev/null || true)"
fi
[[ -n "$QT_BIN" ]] && export PATH="$QT_BIN:$PATH"

# APPIMAGE_EXTRACT_AND_RUN avoids the FUSE requirement (linuxdeploy itself is
# an AppImage and won't run in environments without FUSE support).
export APPIMAGE_EXTRACT_AND_RUN=1
export OUTPUT="$BUILD_DIR/claude-box-$ARCH.AppImage"

"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --plugin qt \
    --output appimage

APPIMAGE="$(find "$BUILD_DIR" -maxdepth 1 -name "claude-box-*.AppImage" | sort -Vr | head -1)"
[[ -f "$APPIMAGE" ]] || die "AppImage not found after linuxdeploy run"
ok "AppImage: $APPIMAGE"

# ---------------------------------------------------------------------------
# .deb via CPack (Debian/Ubuntu)
# ---------------------------------------------------------------------------
if [[ "$PKG_MANAGER" == "apt" ]]; then
    info "Checking dpkg-dev for .deb generation"
    installed dpkg-dev || install_pkgs dpkg-dev

    info "Building .deb"
    (cd "$BUILD_DIR" && cpack -G DEB)
    DEB="$(find "$BUILD_DIR" -maxdepth 1 -name "*.deb" | sort -Vr | head -1)"
    [[ -f "$DEB" ]] && ok ".deb: $DEB" || warn ".deb build produced no file"
fi

# ---------------------------------------------------------------------------
# .rpm via CPack (Fedora/RHEL)
# ---------------------------------------------------------------------------
if [[ "$PKG_MANAGER" == "dnf" ]]; then
    info "Checking rpm-build for .rpm generation"
    installed rpm-build || install_pkgs rpm-build

    info "Building .rpm"
    (cd "$BUILD_DIR" && cpack -G RPM)
    RPM="$(find "$BUILD_DIR" -maxdepth 1 -name "*.rpm" | sort -Vr | head -1)"
    [[ -f "$RPM" ]] && ok ".rpm: $RPM" || warn ".rpm build produced no file"
fi

echo ""
info "Artifacts in $BUILD_DIR:"
find "$BUILD_DIR" -maxdepth 1 \( -name "*.AppImage" -o -name "*.deb" -o -name "*.rpm" \) \
    | sort | while read -r f; do printf '    %s\n' "$f"; done
