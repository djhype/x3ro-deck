#!/bin/sh
# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs x3ro deck through your distribution's package manager, from the
# signed openSUSE Build Service repository (Arch, Debian, Ubuntu, Fedora,
# openSUSE).
# Read it first:
#   curl -fsSLO https://x3ro.life/install.sh && less install.sh && sh install.sh
#
# Everything runs from main() on the last line, so a truncated download does
# nothing.

set -eu

# The OBS project; repositories are $OBS/<distro>/.
OBS=${X3RO_DECK_OBS:-https://download.opensuse.org/repositories/home:/x3ro:/deck}

say() { printf '%s\n' "$*"; }
fail() { say "x3ro-deck install: $*" >&2; exit 1; }

as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        fail "run this as root, or install sudo"
    fi
}

from_source() {
    say "x3ro deck has no package for $1 yet. Build it from source:"
    say ""
    say "  git clone https://github.com/djhype/x3ro-deck && cd x3ro-deck"
    say "  cmake --preset linux && cmake --build --preset linux && cmake --install build"
    say ""
    say "It needs Qt 6.4 or newer (base and declarative), libvterm, CMake and Ninja;"
    say "CONTRIBUTING.md lists the packages."
    exit 2
}

install_deb() {
    # $1: the OBS repository name, e.g. xUbuntu_24.04 or Debian_12.
    repo="$OBS/$1"
    keyring=/etc/apt/keyrings/x3ro-deck.asc
    as_root mkdir -p /etc/apt/keyrings
    curl -fsSL "$repo/Release.key" | as_root tee "$keyring" >/dev/null
    printf 'deb [signed-by=%s] %s/ /\n' "$keyring" "$repo" | as_root tee /etc/apt/sources.list.d/x3ro-deck.list >/dev/null
    as_root apt-get update
    as_root apt-get install -y x3ro-deck
}

install_rpm() {
    # $1: package tool (dnf or zypper), $2: the OBS repository name.
    repo="$OBS/$2"
    if [ "$1" = dnf ]; then
        as_root rpm --import "$repo/repodata/repomd.xml.key"
        printf '[x3ro-deck]\nname=x3ro deck\nbaseurl=%s/\ngpgcheck=1\nrepo_gpgcheck=1\ngpgkey=%s/repodata/repomd.xml.key\nenabled=1\n' \
            "$repo" "$repo" | as_root tee /etc/yum.repos.d/x3ro-deck.repo >/dev/null
        as_root dnf install -y x3ro-deck
    else
        as_root zypper --gpg-auto-import-keys addrepo --refresh "$repo/" x3ro-deck
        as_root zypper --gpg-auto-import-keys install -y x3ro-deck
    fi
}

install_pacman() {
    [ "$(uname -m)" = x86_64 ] || from_source "${PRETTY_NAME:-Arch Linux} on $(uname -m)"
    # OBS names the pacman repository after the project and its repository.
    name=home_x3ro_deck_Arch
    repo="$OBS/Arch"
    key=$(mktemp)
    curl -fsSL "$repo/x86_64/$name.key" -o "$key"
    fpr=$(gpg --with-colons --show-keys "$key" | awk -F: '/^fpr/ { print $10; exit }')
    [ -n "$fpr" ] || fail "could not read the repository key"
    as_root pacman-key --add "$key"
    as_root pacman-key --lsign-key "$fpr"
    rm -f "$key"
    if ! grep -q "^\[$name\]" /etc/pacman.conf; then
        # $arch is pacman's, not ours.
        # shellcheck disable=SC2016
        printf '\n[%s]\nServer = %s/$arch\n' "$name" "$repo" | as_root tee -a /etc/pacman.conf >/dev/null
    fi
    # -Sy alone would be a partial upgrade, which Arch does not support.
    say "Updating the system and installing x3ro-deck (pacman -Syu)."
    as_root pacman -Syu --needed x3ro-deck
}

main() {
    command -v curl >/dev/null 2>&1 || fail "curl is needed"
    [ -r /etc/os-release ] || from_source "this system"
    # shellcheck disable=SC1091
    . /etc/os-release
    id_like="${ID:-} ${ID_LIKE:-}"
    case "${ID:-}" in
        ubuntu) install_deb "xUbuntu_${VERSION_ID}" ;;
        debian) install_deb "Debian_${VERSION_ID%%.*}" ;;
        fedora) install_rpm dnf "Fedora_${VERSION_ID}" ;;
        opensuse-tumbleweed) install_rpm zypper openSUSE_Tumbleweed ;;
        arch | cachyos | endeavouros | manjaro) install_pacman ;;
        *)
            case "$id_like" in
                # Ubuntu derivatives (Mint, Pop!_OS) name their Ubuntu base.
                *ubuntu*)
                    case "${UBUNTU_CODENAME:-}" in
                        noble) install_deb xUbuntu_24.04 ;;
                        oracular) install_deb xUbuntu_24.10 ;;
                        plucky) install_deb xUbuntu_25.04 ;;
                        *) from_source "${PRETTY_NAME:-this distribution}" ;;
                    esac
                    ;;
                *) from_source "${PRETTY_NAME:-this distribution}" ;;
            esac
            ;;
    esac
    say "x3ro deck is installed. Start it from your applications menu or run x3ro-deck."
}

main "$@"
