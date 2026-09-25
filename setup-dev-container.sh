#!/usr/bin/env bash
# Dev container bootstrap — git tools, C++, Java 21, Node.js LTS, Gradle
set -euo pipefail

# ── GitHub CLI repo ──────────────────────────────────────────────────────────
if ! command -v gh &>/dev/null; then
  curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
    | sudo dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg
  sudo chmod go+r /usr/share/keyrings/githubcli-archive-keyring.gpg
  echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] \
https://cli.github.com/packages stable main" \
    | sudo tee /etc/apt/sources.list.d/github-cli.list > /dev/null
  sudo apt-get update -qq
fi

# ── Apt packages ─────────────────────────────────────────────────────────────
sudo apt-get install -y \
  git gh tig lazygit delta bat git-lfs git-filter-repo pre-commit \
  build-essential cmake ninja-build clang gdb valgrind pkg-config \
  openjdk-21-jdk maven \
  python3 python3-pip \
  jq curl wget unzip

# ── nvm + Node.js LTS ────────────────────────────────────────────────────────
if [ ! -d "$HOME/.nvm" ]; then
  curl -fsSL https://raw.githubusercontent.com/nvm-sh/nvm/HEAD/install.sh | bash
fi

export NVM_DIR="$HOME/.nvm"
# shellcheck source=/dev/null
[ -s "$NVM_DIR/nvm.sh" ] && source "$NVM_DIR/nvm.sh"

nvm install --lts
nvm alias default lts/*

# ── SDKMAN + Gradle ──────────────────────────────────────────────────────────
if [ ! -d "$HOME/.sdkman" ]; then
  curl -fsSL https://get.sdkman.io | bash
fi

# shellcheck source=/dev/null
[ -s "$HOME/.sdkman/bin/sdkman-init.sh" ] && source "$HOME/.sdkman/bin/sdkman-init.sh"

sdk install gradle

# ── Summary ──────────────────────────────────────────────────────────────────
echo ""
echo "Versions installed:"
git --version
gh --version | head -1
node --version
npm --version
java -version 2>&1 | head -1
mvn --version | head -1
gradle --version | grep "^Gradle"
