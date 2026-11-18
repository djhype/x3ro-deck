# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs x3ro deck on Windows through winget, or, where winget is missing,
# from the latest release's installer (checked against its published SHA-256).
$ErrorActionPreference = 'Stop'
if (Get-Command winget -ErrorAction SilentlyContinue) {
    winget install --id x3ro.deck --exact --source winget --accept-package-agreements --accept-source-agreements
    exit $LASTEXITCODE
}
$release = Invoke-RestMethod https://api.github.com/repos/x3ro/x3ro-deck/releases/latest
$asset = $release.assets | Where-Object name -like 'x3ro-deck-*-setup.exe' | Select-Object -First 1
$sums = (Invoke-RestMethod ($release.assets | Where-Object name -eq 'SHA256SUMS').browser_download_url)
$file = Join-Path $env:TEMP $asset.name
Invoke-WebRequest $asset.browser_download_url -OutFile $file
$expected = ($sums -split "`n" | Where-Object { $_ -match [regex]::Escape($asset.name) }) -replace '\s.*$', ''
if ((Get-FileHash $file -Algorithm SHA256).Hash -ne $expected.ToUpper()) { throw "Checksum mismatch for $($asset.name)" }
Start-Process $file -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES' -Wait
Write-Host "x3ro deck is installed. Start it from the Start menu."
