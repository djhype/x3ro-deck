# Packaging

| Folder | What |
|---|---|
| `arch/` | AUR PKGBUILDs (`x3ro-deck`, `x3ro-deck-bin`) |
| `debian/`, `rpm/`, `obs/` | The openSUSE Build Service packages and repositories |
| `macos/`, `homebrew/` | The notarized DMG and the Homebrew cask |
| `windows/`, `winget/` | The Inno Setup installer and the winget manifest |
| `install.sh`, `install.ps1` | One-line installers served from `https://x3ro.life/` |

`install.sh` and `install.ps1` are served as static files from x3ro.life over
HTTPS (any static host works; the release workflow uploads them). Keep the
domain on auto-renew: the README points at it.
