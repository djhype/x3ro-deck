#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later
#
# release.sh prepare <version>: moves CHANGELOG.md's Unreleased section under
#   <version> and writes the version into CMakeLists.txt and the package
#   recipes, then commits (on a release-<version> branch when run on main), so
#   the release goes through a pull request like any change. A pre-release
#   (1.2.0-rc.1) only sets CMake's version and keeps Unreleased, whose text
#   becomes its notes.
# release.sh tag <version>: after that commit is on main, checks it and makes
#   the annotated tag v<version>. Pushing the tag starts the release workflow.
# release.sh check <tag>: what `tag` checks, also run by the release workflow:
#   CMake's version is the tag's (without -rc.N) and the notes are not empty.
set -euo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"

die() {
    printf 'release.sh: %s\n' "$*" >&2
    exit 1
}

usage() {
    sed -n '4,14p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

[[ $# -eq 2 ]] || usage
command=$1
version=${2#v}
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]] || die "not a version: $2"
base=${version%%-*}
prerelease=$([[ $version == *-* ]] && echo 1 || echo)

cmake_version() {
    sed -n 's/^project(x3ro-deck VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt
}

has_section() {
    grep -q "^## \[$1\]" CHANGELOG.md
}

unreleased() {
    awk '/^## \[/ { inside = ($0 ~ /^## \[Unreleased\]/); next } inside' CHANGELOG.md
}

blank() {
    [[ -z ${1//[[:space:]]/} ]]
}

previous_tag() {
    git describe --tags --abbrev=0 --exclude '*-*' 2>/dev/null || true
}

empty_unreleased() {
    local since
    since=$(previous_tag)
    {
        echo "CHANGELOG.md has nothing under [Unreleased]. Write what users will"
        echo "notice there first. Commits since ${since:-the first commit}:"
        echo
        git log --no-merges --format='  - %s' ${since:+"$since..HEAD"}
    } >&2
    exit 1
}

check() {
    local found notes
    found=$(cmake_version)
    [[ $found == "$base" ]] || die "CMakeLists.txt says $found, the tag says $base (run: scripts/release.sh prepare $version)"
    if [[ -z $prerelease ]]; then
        has_section "$base" || die "CHANGELOG.md has no [$base] section (run: scripts/release.sh prepare $version)"
    fi
    notes=$(scripts/release-notes.sh "$version")
    blank "$notes" && die "the release notes for $version are empty"
    return 0
}

# Writes the version into the recipes. OBS's set_version does the same on the
# server, and the release workflow for the AUR and the cask; this keeps the
# tree saying what was released and gives Debian and RPM a changelog entry.
bump_packaging() {
    local day deb_date rpm_date pkg old
    day=$(date +%Y-%m-%d)
    deb_date=$(LC_ALL=C date '+%a, %d %b %Y %H:%M:%S %z')
    rpm_date=$(LC_ALL=C date '+%a %b %d %Y')

    for pkg in x3ro-deck x3ro-deck-bin; do
        old=$(sed -n 's/^pkgver=//p' "packaging/arch/$pkg/PKGBUILD")
        V=$base perl -pi -e 's/^pkgver=.*/pkgver=$ENV{V}/; s/^pkgrel=.*/pkgrel=1/' "packaging/arch/$pkg/PKGBUILD"
        O=$old V=$base perl -pi -e 's/\Q$ENV{O}\E(?![0-9])/$ENV{V}/g; s/^(\tpkgrel = ).*/${1}1/' "packaging/arch/$pkg/.SRCINFO"
    done
    V=$base perl -pi -e 's/^(  version ")[^"]*"/$1$ENV{V}"/' packaging/homebrew/x3ro-deck.rb
    V=$base perl -pi -e 's/^Version: .*/Version: $ENV{V}-1/' packaging/obs/x3ro-deck.dsc
    V=$base perl -pi -e 's/^(Version:\s+).*/$1$ENV{V}/; s/^(Release:\s+)\d+/${1}1/' packaging/rpm/x3ro-deck.spec
    V=$base D=$rpm_date perl -0pi -e 's/^%changelog\n/%changelog\n* $ENV{D} x3ro - $ENV{V}-1\n- Release $ENV{V}; see CHANGELOG.md.\n\n/m' \
        packaging/rpm/x3ro-deck.spec
    {
        printf 'x3ro-deck (%s-1) unstable; urgency=medium\n\n' "$base"
        printf '  * Release %s; see CHANGELOG.md.\n\n' "$base"
        printf ' -- x3ro <noreply@x3ro.life>  %s\n\n' "$deb_date"
        cat packaging/debian/changelog
    } > packaging/debian/changelog.new
    mv packaging/debian/changelog.new packaging/debian/changelog

    V=$base D=$day perl -pi -e 's/^## \[Unreleased\]\s*$/## [Unreleased]\n\n## [$ENV{V}] - $ENV{D}\n/' CHANGELOG.md
}

prepare() {
    [[ -z $(git status --porcelain) ]] || die "the working tree has changes; commit or stash them first"
    git tag -l "v$version" | grep -q . && die "v$version already exists"
    [[ -z $prerelease ]] && has_section "$base" && die "CHANGELOG.md already has a [$base] section"
    blank "$(unreleased)" && empty_unreleased

    local branch
    branch=$(git rev-parse --abbrev-ref HEAD)
    if [[ $branch == main ]]; then
        git switch -c "release-$version"
    fi

    V=$base perl -pi -e 's/^(project\(x3ro-deck VERSION )[0-9.]+/$1$ENV{V}/' CMakeLists.txt
    [[ -n $prerelease ]] || bump_packaging

    if [[ -z $(git status --porcelain) ]]; then
        echo "Nothing to change: CMakeLists.txt already says $base."
        echo "Tag main with: scripts/release.sh tag $version"
        return
    fi
    git commit -aqs -m "Release $version"
    git --no-pager show --stat --format='%s' HEAD
    echo
    echo "Check the commit, then open a pull request for $(git rev-parse --abbrev-ref HEAD)."
    echo "Once it is merged: git switch main && git pull && scripts/release.sh tag $version"
}

tag() {
    [[ -z $(git status --porcelain) ]] || die "the working tree has changes"
    [[ $(git rev-parse --abbrev-ref HEAD) == main ]] || die "tag from main, after the release commit is merged"
    git tag -l "v$version" | grep -q . && die "v$version already exists"
    check
    git tag -a "v$version" -m "x3ro deck $version"
    echo "Tagged v$version at $(git rev-parse --short HEAD). Release notes:"
    echo
    scripts/release-notes.sh "$version" | sed 's/^/    /'
    echo
    echo "Start the release with: git push origin v$version"
}

case $command in
prepare) prepare ;;
tag) tag ;;
check) check && echo "v$version is ready: CMake $base, notes found." ;;
*) usage ;;
esac
