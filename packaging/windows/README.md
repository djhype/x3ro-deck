# Windows release

`scripts/windows-package.ps1 -Version <v>` (Developer PowerShell, Qt for
MSVC, Inno Setup 6) builds, deploys Qt with `windeployqt`, signs, and makes
`x3ro-deck-<v>-setup.exe`: a per-user install with a Start menu shortcut
carrying AppUserModelID `life.x3ro.deck` (toasts need it). Uninstalling stops
the app and `x3ro-deck-sessiond` first.

Signing: Azure Trusted Signing (`AZURE_SIGNING_ENDPOINT`, `_ACCOUNT`,
`_PROFILE` plus Azure login) or a PFX (`WINDOWS_CERT_PFX`,
`WINDOWS_CERT_PASSWORD`). An EV or Trusted Signing certificate avoids the
SmartScreen "unknown publisher" warning.

winget: `packaging/winget/manifests/…` is the manifest set for `x3ro.deck`.
The release workflow fills in the installer's URL and SHA-256; submit it to
`microsoft/winget-pkgs` with `wingetcreate update x3ro.deck --version <v>
--urls <url> --submit`. `packaging/install.ps1` installs through winget, or
from the release installer after checking its checksum.
