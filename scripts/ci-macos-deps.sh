#!/usr/bin/env bash
# Build and test dependencies for the macOS CI job (Homebrew).
set -euo pipefail
brew update --quiet
brew install --quiet qt libvterm ninja pkg-config tmux
echo "CMAKE_PREFIX_PATH=$(brew --prefix qt)" >> "${GITHUB_ENV:-/dev/null}"
