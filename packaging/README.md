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
