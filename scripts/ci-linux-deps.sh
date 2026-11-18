#!/usr/bin/env bash
# Installs the Linux CI toolchain in an archlinux container. Arch is the one
# common image that ships Qt 6.6+ and every KF6 framework the app links.
set -euo pipefail

pacman -Syu --noconfirm --needed \
    base-devel git cmake ninja clang \
    qt6-base qt6-declarative \
    kglobalaccel kwindowsystem \
    tmux libvterm dbus ttf-dejavu
