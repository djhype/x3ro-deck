#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later
#
# check-spdx.sh [--fix]: every tracked source file (C++, Objective-C++, QML,
# CMake, shell and Python) carries the SPDX copyright and license lines in
# its first lines. --fix adds them where they are missing.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

copyright='SPDX-FileCopyrightText: 2026 x3ro'
license='SPDX-License-Identifier: GPL-3.0-or-later'
fix=0
[[ ${1:-} == --fix ]] && fix=1

mapfile -t files < <(git ls-files -- '*.cpp' '*.h' '*.mm' '*.qml' 'CMakeLists.txt' '*/CMakeLists.txt' '*.cmake' '*.sh' '*.py' \
    ':!tests/fixtures/**')
missing=()
for f in "${files[@]}"; do
    [[ -f $f ]] || continue
    if head -n 6 "$f" | grep -q "$license"; then
        continue
    fi
    if (( fix )); then
        case $f in
            *.cpp|*.h|*.mm|*.qml) prefix='//' ;;
            *) prefix='#' ;;
        esac
        header="$prefix $copyright"$'\n'"$prefix $license"
        if head -n 1 "$f" | grep -q '^#!'; then
            { head -n 1 "$f"; printf '%s\n' "$header"; tail -n +2 "$f"; } > "$f.spdx"
        else
            { printf '%s\n\n' "$header"; cat "$f"; } > "$f.spdx"
        fi
        chmod --reference="$f" "$f.spdx"
        mv "$f.spdx" "$f"
    else
        missing+=("$f")
    fi
done
if (( ${#missing[@]} )); then
    printf 'missing SPDX header: %s\n' "${missing[@]}" >&2
    echo "Run scripts/check-spdx.sh --fix to add them." >&2
    exit 1
fi
