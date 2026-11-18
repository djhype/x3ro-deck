#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later
#
# prepare-public-history.sh <out-dir> [--keep-authors]
#
# Makes a rewritten copy of this repository for the public remote. The
# original is never touched: it clones into <out-dir>, then git filter-repo
# replaces private names in every file, path and commit message, and (unless
# --keep-authors) sets every author and committer to the project identity.
# Finally it scans the full history and fails if anything is left.
#
# The replacements live in .git/info/public-history-replacements (one
# `old==>new` per line, git filter-repo's --replace-text format), outside the
# tracked tree on purpose: committing the list would publish the names.
# X3RO_DECK_PUBLIC_PATTERN overrides the scan's regex (grep -E, ignoring case).
#
# Pushing is left to you; see packaging/PUBLISHING.md.
set -euo pipefail
out=${1:?usage: prepare-public-history.sh <out-dir> [--keep-authors]}
keep_authors=${2:-}
root=$(git rev-parse --show-toplevel)
replacements="$(git -C "$root" rev-parse --absolute-git-dir)/info/public-history-replacements"
identity_name="x3ro"
identity_email="noreply@x3ro.life"
pattern=${X3RO_DECK_PUBLIC_PATTERN:-}

command -v git-filter-repo >/dev/null || { echo "needs git-filter-repo (pip install git-filter-repo)" >&2; exit 1; }
[[ -s $replacements ]] || { echo "missing $replacements" >&2; exit 1; }
[[ ! -e $out ]] || { echo "$out exists; pick a new folder" >&2; exit 1; }
if [[ -z $pattern ]]; then
    # Every replaced word, as one alternation.
    pattern=$(sed -n 's/==>.*//p' "$replacements" | sed 's/[][\.*^$+?(){}|/]/\\&/g' | paste -sd'|')
    # The personal identity, which only the author rewrite removes.
    [[ $keep_authors == --keep-authors ]] || pattern+="|$(git -C "$root" log -1 --format=%ae | sed 's/[.]/\\./g')"
fi

git clone --quiet --no-local "$root" "$out"
cd "$out"

# Paths: the same replacements, applied to each file name.
path_callback=$(python3 - "$replacements" <<'PY'
import sys
pairs = [l.rstrip("\n").split("==>", 1) for l in open(sys.argv[1]) if "==>" in l]
print("pairs = %r\nfor old, new in pairs:\n    filename = filename.replace(old.encode(), new.encode())\nreturn filename" % pairs)
PY
)
args=(--force --replace-text "$replacements" --replace-message "$replacements" --filename-callback "$path_callback")
if [[ $keep_authors != --keep-authors ]]; then
    args+=(--name-callback "return b'$identity_name'" --email-callback "return b'$identity_email'")
fi
git filter-repo "${args[@]}"

# filter-repo drops the origin remote, so a push cannot reach the private one.
left=$( { git log -p --all; git log --all --format='%an %ae %cn %ce'; git log --all --name-only --format=; } |
    grep -ciE "$pattern" || true)
if [[ $left != 0 ]]; then
    echo "$left lines still match; inspect with: git -C $out log -p --all | grep -niE '$pattern'" >&2
    exit 1
fi
echo "Rewritten history in $out: $(git rev-list --all --count) commits, nothing matches."
