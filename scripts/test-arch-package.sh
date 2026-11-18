#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later
#
# test-arch-package.sh: builds packaging/arch/x3ro-deck/PKGBUILD against a
# `git archive` of HEAD instead of the release tarball, in a temporary folder,
# and lists the package's files. Runs namcap when it is installed.
set -euo pipefail
root=$(git rev-parse --show-toplevel)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pkgver=$(sed -n 's/^pkgver=//p' "$root/packaging/arch/x3ro-deck/PKGBUILD")
git -C "$root" archive --prefix="x3ro-deck-$pkgver/" -o "$work/x3ro-deck-$pkgver.tar.gz" HEAD
sed "s|^source=.*|source=(\"x3ro-deck-$pkgver.tar.gz\")|" "$root/packaging/arch/x3ro-deck/PKGBUILD" > "$work/PKGBUILD"
(cd "$work" && makepkg -f --nodeps --noconfirm >"$work/makepkg.log" 2>&1) || { tail -40 "$work/makepkg.log"; exit 1; }
pkg=$(ls "$work"/x3ro-deck-*.pkg.tar.zst)
tar -tf "$pkg" | grep -v '/$' | sort
if command -v namcap >/dev/null; then
    namcap "$work/PKGBUILD" "$pkg"
fi
