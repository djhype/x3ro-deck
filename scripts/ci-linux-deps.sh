#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later
# Installs the Linux CI toolchain in an archlinux container: Qt and the
# build and test tools, no KDE Frameworks.
set -euo pipefail

pacman -Syu --noconfirm --needed \
    base-devel git cmake ninja clang \
    qt6-base qt6-declarative \
    tmux libvterm dbus ttf-dejavu
