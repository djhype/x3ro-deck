#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later
#
# test-deb-package.sh: builds the .deb from HEAD in an ubuntu:24.04 container
# (Qt 6.4, the floor) and installs it there, then takes an offscreen
# --demo screenshot with the installed binary.
set -euo pipefail
root=$(git rev-parse --show-toplevel)
work=$(mktemp -d)
# The container writes as root; it hands the folder back before cleanup.
trap 'docker run --rm -v "$work:/work" ubuntu:24.04 chown -R "$(id -u):$(id -g)" /work >/dev/null 2>&1; rm -rf "$work"' EXIT
git -C "$root" archive --prefix=x3ro-deck/ -o "$work/src.tar" HEAD
docker run --rm -v "$work:/work" ubuntu:24.04 bash -euc '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends build-essential devscripts equivs ca-certificates >/work/deps.log 2>&1 || { tail -20 /work/deps.log; exit 1; }
    cd /work && tar xf src.tar && cd x3ro-deck && cp -r packaging/debian debian
    mk-build-deps -i -r -t "apt-get -y -qq --no-install-recommends" debian/control >/work/deps.log 2>&1 || { tail -20 /work/deps.log; exit 1; }
    dpkg-buildpackage -b -us -uc >/work/build.log 2>&1 || { tail -40 /work/build.log; exit 1; }
    apt-get install -y -qq --no-install-recommends /work/x3ro-deck_*.deb dbus fonts-dejavu-core >/work/install.log 2>&1 || { tail -20 /work/install.log; exit 1; }
    dpkg -L x3ro-deck | grep -E "bin/|applications/"
    export XDG_RUNTIME_DIR=$(mktemp -d) QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
    chmod 700 "$XDG_RUNTIME_DIR"
    dbus-run-session -- x3ro-deck --demo --screenshot /work/demo.png 5000 >/dev/null 2>&1
    test -s /work/demo.png && echo "installed binary runs: screenshot ok"
'
