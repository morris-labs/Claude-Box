#!/bin/bash
# Build Claude Box for Linux.
# Run from anywhere; the script locates the repo via its own path.
# Installs missing prerequisites via apt, dnf, or pacman.
# Ubuntu 22.04+ / Fedora 36+ / Arch are the tested targets.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
GUI_DIR="$REPO_DIR/gui"

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
    PKGS=(cmake build-essential pkg-config qt6-base-dev libvterm-dev)
    installed() { dpkg -s "$1" >/dev/null 2>&1; }
    install_pkgs() { sudo apt-get install -y "$@"; }
elif have dnf; then
    PKG_MANAGER=dnf
    PKGS=(cmake gcc-c++ make pkg-config qt6-qtbase-devel libvterm-devel)
    installed() { rpm -q "$1" >/dev/null 2>&1; }
    install_pkgs() { sudo dnf install -y "$@"; }
elif have pacman; then
    PKG_MANAGER=pacman
    PKGS=(cmake base-devel pkgconf qt6-base libvterm)
    installed() { pacman -Q "$1" >/dev/null 2>&1; }
    install_pkgs() { sudo pacman -S --noconfirm "$@"; }
else
    die "No supported package manager found (apt-get, dnf, pacman)"
fi
ok "Package manager: $PKG_MANAGER"

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------
info "Checking prerequisites"
MISSING=()
for pkg in "${PKGS[@]}"; do
    installed "$pkg" || MISSING+=("$pkg")
done

if [[ ${#MISSING[@]} -gt 0 ]]; then
    info "Installing: ${MISSING[*]}"
    install_pkgs "${MISSING[@]}"
else
    ok "All prerequisites present"
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
info "Configuring"
cmake -B "$GUI_DIR/build" "$GUI_DIR"

info "Building"
cmake --build "$GUI_DIR/build" --parallel "$(nproc)"

ok "Done: $GUI_DIR/build/claude-box-gui"
echo ""
echo "To register the desktop entry and dock icon, run:"
echo "  cmake --build $GUI_DIR/build --target desktop-install"
