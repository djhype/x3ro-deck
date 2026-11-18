# macOS release

`scripts/macos-package.sh <version>` on a Mac builds the universal app, deploys
Qt into it, signs, notarizes, staples and makes `x3ro-deck-<version>.dmg`. The
release workflow runs it with these repository secrets:

| Secret | What |
|---|---|
| `MACOS_CERTIFICATE_P12`, `MACOS_CERTIFICATE_PASSWORD` | The Developer ID Application certificate, base64 |
| `MACOS_SIGN_IDENTITY` | `Developer ID Application: <name> (<TEAMID>)` |
| `NOTARY_APPLE_ID`, `NOTARY_TEAM_ID`, `NOTARY_PASSWORD` | Notarization credentials (app-specific password) |

The Homebrew cask (`packaging/homebrew/x3ro-deck.rb`) goes into the
`djhype/homebrew-tap` repository as `Casks/x3ro-deck.rb`, so users run
`brew install --cask djhype/tap/x3ro-deck`.
