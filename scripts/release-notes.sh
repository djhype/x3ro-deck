#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later
#
# release-notes.sh <version>: prints CHANGELOG.md's section for <version>
# (without a leading v and any -rcN), else the Unreleased section.
set -euo pipefail
version=${1#v}
base=${version%%-*}
root=$(git rev-parse --show-toplevel)
section() {
    awk -v want="$1" '
        /^## \[/ { inside = index($0, "[" want "]") > 0; next }
        inside { print }
    ' "$root/CHANGELOG.md"
}
notes=$(section "$base")
[[ -n ${notes//[[:space:]]/} ]] || notes=$(section Unreleased)
printf '%s\n' "$notes" | sed -e '/./,$!d'
