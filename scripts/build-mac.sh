#!/bin/bash
# Build Claude Box for macOS.
# Run from anywhere; the script locates the repo via its own path.
# Installs all missing prerequisites via Homebrew where possible.
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
# Xcode command-line tools
# ---------------------------------------------------------------------------
info "Checking Xcode command-line tools"
if ! xcode-select -p >/dev/null 2>&1; then
    info "Installing Xcode command-line tools (follow the popup)"
    xcode-select --install
    echo "Re-run this script once the Xcode tools installation completes."
    exit 0
fi
ok "Xcode CLT at $(xcode-select -p)"

# ---------------------------------------------------------------------------
# Homebrew
# ---------------------------------------------------------------------------
info "Checking Homebrew"
if ! have brew; then
    info "Installing Homebrew"
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
fi
# Ensure Homebrew is on PATH for this session (covers both Apple Silicon and Intel).
for brew_path in /opt/homebrew/bin/brew /usr/local/bin/brew; do
    [[ -x "$brew_path" ]] && eval "$("$brew_path" shellenv)" && break
done
ok "brew $(brew --version | head -1)"

# ---------------------------------------------------------------------------
# Brew packages (cmake, pkg-config, libvterm, librsvg)
# librsvg provides rsvg-convert for icon generation.
# qt6 is handled separately below since it may come from the Qt installer.
# ---------------------------------------------------------------------------
BREW_PKGS=(cmake pkg-config libvterm librsvg)
for pkg in "${BREW_PKGS[@]}"; do
    info "Checking $pkg"
    if brew list "$pkg" >/dev/null 2>&1; then
        ok "$pkg already installed"
    else
        info "Installing $pkg"
        brew install "$pkg"
    fi
done

# ---------------------------------------------------------------------------
# Qt 6: Homebrew or Qt Online Installer
# ---------------------------------------------------------------------------
info "Checking Qt 6"

find_qt_prefix() {
    # 1. Homebrew qt6 or qt package.
    for name in qt6 qt; do
        if brew list "$name" >/dev/null 2>&1; then
            local p
            p="$(brew --prefix "$name" 2>/dev/null)" || continue
            [[ -x "$p/bin/qmake" ]] && echo "$p" && return 0
        fi
    done
    # 2. Qt Online Installer: ~/Qt/x.y.z/{macos,clang_64} or /Applications/Qt/...
    local root
    for root in "$HOME/Qt" "/Applications/Qt"; do
        [[ -d "$root" ]] || continue
        # Sort version directories highest-first, pick first that has qmake.
        while IFS= read -r ver_dir; do
            for sub in macos clang_64; do
                [[ -x "$ver_dir/$sub/bin/qmake" ]] && echo "$ver_dir/$sub" && return 0
            done
        done < <(find "$root" -maxdepth 1 -name "[0-9]*.[0-9]*.[0-9]*" -type d 2>/dev/null | sort -Vr)
    done
    return 1
}

QT_PREFIX=""
if ! QT_PREFIX="$(find_qt_prefix)"; then
    info "Qt 6 not found -- installing via Homebrew"
    brew install qt6
    QT_PREFIX="$(brew --prefix qt6)"
fi
export PATH="$QT_PREFIX/bin:$PATH"
ok "Qt at $QT_PREFIX ($(qmake --version | tail -1))"

# ---------------------------------------------------------------------------
# Resolve pkg-config paths for libvterm (Homebrew or system).
# ---------------------------------------------------------------------------
VTERM_PC=""
for dir in \
    "$(brew --prefix libvterm 2>/dev/null)/lib/pkgconfig" \
    /usr/local/lib/pkgconfig \
    /opt/local/lib/pkgconfig; do
    [[ -f "$dir/vterm.pc" ]] && VTERM_PC="$dir" && break
done
[[ -z "$VTERM_PC" ]] && die "libvterm pkg-config file not found -- try: brew install libvterm"
ok "libvterm pkgconfig at $VTERM_PC"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
info "Configuring"
cmake -B "$GUI_DIR/build" "$GUI_DIR" \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
    -DPKG_CONFIG_PATH="$VTERM_PC"

info "Building"
cmake --build "$GUI_DIR/build" --parallel "$(sysctl -n hw.logicalcpu)"

# generate-icns requires rsvg-convert (librsvg) and iconutil (Xcode CLT).
# Skip with a warning rather than failing the whole build if either is absent.
if have rsvg-convert && have iconutil; then
    info "Generating .icns icon"
    cmake --build "$GUI_DIR/build" --target generate-icns
else
    warn "rsvg-convert or iconutil not found -- skipping icon generation"
    warn "Install librsvg (brew install librsvg) and Xcode CLT to include the dock icon"
fi

info "Running macdeployqt and ad-hoc signing"
cmake --build "$GUI_DIR/build" --target app-bundle

info "Creating DMG"
cmake --build "$GUI_DIR/build" --target dmg

# Locate the DMG by glob so the name doesn't have to be hardcoded here.
DMG="$(find "$GUI_DIR/build" -maxdepth 1 -name "*.dmg" | sort -Vr | head -1)"
if [[ -f "$DMG" ]]; then
    ok "Done: $DMG"
else
    die "DMG not found after build -- check output above"
fi
