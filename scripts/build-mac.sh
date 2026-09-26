#!/bin/bash
# Build Claude Box for macOS.
# Run from anywhere; the script locates the repo via its own path.
# Installs all missing prerequisites via Homebrew.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
GUI_DIR="$REPO_DIR/gui"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
info()  { printf '\033[1;34m==> %s\033[0m\n' "$*"; }
ok()    { printf '\033[1;32m    OK: %s\033[0m\n' "$*"; }
die()   { printf '\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------------------
# Xcode command-line tools
# ---------------------------------------------------------------------------
info "Checking Xcode command-line tools"
if ! xcode-select -p >/dev/null 2>&1; then
    info "Installing Xcode command-line tools (follow the popup)"
    xcode-select --install
    echo "Re-run this script once the Xcode tools installation completes."
    exit 0
fi
ok "Xcode CLT present at $(xcode-select -p)"

# ---------------------------------------------------------------------------
# Homebrew
# ---------------------------------------------------------------------------
info "Checking Homebrew"
if ! have brew; then
    info "Installing Homebrew"
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    # Add Homebrew to PATH for the rest of this session (Apple Silicon default path).
    eval "$(/opt/homebrew/bin/brew shellenv 2>/dev/null || /usr/local/bin/brew shellenv)"
fi
ok "brew $(brew --version | head -1)"

# ---------------------------------------------------------------------------
# Build dependencies
# ---------------------------------------------------------------------------
BREW_PKGS=(cmake pkg-config libvterm librsvg qt6)

for pkg in "${BREW_PKGS[@]}"; do
    info "Checking $pkg"
    if brew list "$pkg" >/dev/null 2>&1; then
        ok "$pkg already installed"
    else
        info "Installing $pkg"
        brew install "$pkg"
    fi
done

# Put Qt6's bin directory on PATH so cmake finds macdeployqt.
QT6_BIN="$(brew --prefix qt6)/bin"
export PATH="$QT6_BIN:$PATH"

ok "macdeployqt at $(which macdeployqt)"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
info "Configuring"
cmake -B "$GUI_DIR/build" "$GUI_DIR" \
    -DCMAKE_PREFIX_PATH="$(brew --prefix qt6)" \
    -DPKG_CONFIG_PATH="$(brew --prefix libvterm)/lib/pkgconfig"

info "Building"
cmake --build "$GUI_DIR/build" --parallel "$(sysctl -n hw.logicalcpu)"

info "Generating .icns icon"
cmake --build "$GUI_DIR/build" --target generate-icns

info "Running macdeployqt and ad-hoc signing"
cmake --build "$GUI_DIR/build" --target app-bundle

info "Creating DMG"
cmake --build "$GUI_DIR/build" --target dmg

DMG="$GUI_DIR/build/Claude Box.dmg"
if [[ -f "$DMG" ]]; then
    ok "Done: $DMG"
else
    die "DMG not found after build -- check output above"
fi
