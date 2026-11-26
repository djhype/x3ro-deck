# Packaging

| Folder | What |
|---|---|
| `arch/` | AUR PKGBUILDs (`x3ro-deck`, `x3ro-deck-bin`) |
| `debian/`, `rpm/`, `obs/` | The openSUSE Build Service packages and repositories |
| `macos/`, `homebrew/` | The notarized DMG and the Homebrew cask |
| `windows/`, `winget/` | The Inno Setup installer and the winget manifest |
| `install.sh`, `install.ps1` | One-line installers served from `https://deck.x3ro.life/` |

`install.sh` and `install.ps1` are served as static files from deck.x3ro.life over
HTTPS by GitHub Pages (`.github/workflows/pages.yml`, which publishes them
whenever they change on main; any static host works). Keep the
domain on auto-renew: the README points at it.

## Releasing

1. Every change users will notice adds a line under *Unreleased* in
   `CHANGELOG.md`. That section becomes the GitHub release notes.
2. `scripts/release.sh prepare 1.2.3` moves *Unreleased* under `[1.2.3]`,
   writes the version into `CMakeLists.txt` (the About dialog, the macOS
   bundle, the Windows version info) and the package recipes, and commits on a
   `release-1.2.3` branch. With *Unreleased* empty it lists the commits since
   the last release instead. Open a pull request for it.
3. Once it is merged, `git switch main && git pull && scripts/release.sh tag 1.2.3`,
   then `git push origin v1.2.3`.
4. The release workflow checks the tag again (`scripts/release.sh check`),
   builds, waits for approval in the `release` environment and publishes.

A pre-release (`1.2.3-rc.1`) only sets CMake's version and keeps
*Unreleased*, which becomes its notes; it is marked as a pre-release and
skips OBS and winget. To fix the notes of a published release, edit
`CHANGELOG.md` and run
`gh release edit v1.2.3 --notes-file <(scripts/release-notes.sh v1.2.3)`.
