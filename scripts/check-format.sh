#!/usr/bin/env bash
# Fails when a C++ source differs from .clang-format. Run with --fix to rewrite.
set -euo pipefail
cd "$(dirname "$0")/.."

mapfile -t files < <(git ls-files --cached --others --exclude-standard '*.cpp' '*.h')
if [[ ${1:-} == --fix ]]; then
    clang-format -i "${files[@]}"
else
    clang-format --dry-run --Werror "${files[@]}"
fi
