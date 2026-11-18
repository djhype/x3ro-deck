# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later
#
# windows-package.ps1 -Version <v>: Release build, windeployqt, Authenticode
# signing and the Inno Setup installer. Run from a Visual Studio Developer
# PowerShell with Qt for MSVC on CMAKE_PREFIX_PATH and Inno Setup 6 installed.
#
# Signing uses Azure Trusted Signing when AZURE_SIGNING_* is set (the
# trusted-signing-cli), else a PFX in WINDOWS_CERT_PFX / WINDOWS_CERT_PASSWORD,
# else it skips signing (test builds).
param([Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
$root = (git rev-parse --show-toplevel)
$build = "$root/build-release-windows"
$deploy = "$build/deploy"

cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DX3RO_DECK_BUNDLED_VTERM=ON
cmake --build $build
cmake --install $build --prefix $deploy
Move-Item "$deploy/bin/*" $deploy -Force
Remove-Item "$deploy/bin" -Recurse -Force
windeployqt --release --qmldir "$root/qml" "$deploy/x3ro-deck.exe"
Set-Content "$deploy/Qt-SOURCE.txt" "Qt source: https://download.qt.io/official_releases/qt/"

function Sign([string[]]$Files) {
    if ($env:AZURE_SIGNING_ENDPOINT) {
        foreach ($f in $Files) {
            trusted-signing-cli -e $env:AZURE_SIGNING_ENDPOINT -a $env:AZURE_SIGNING_ACCOUNT -c $env:AZURE_SIGNING_PROFILE $f
        }
    } elseif ($env:WINDOWS_CERT_PFX) {
        signtool sign /f $env:WINDOWS_CERT_PFX /p $env:WINDOWS_CERT_PASSWORD /fd sha256 /tr http://timestamp.digicert.com /td sha256 $Files
    } else {
        Write-Warning "No signing credentials; the installer is unsigned."
    }
}
Sign (Get-ChildItem $deploy -Recurse -Include *.exe, *.dll | ForEach-Object FullName)

& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" "/DAppVersion=$Version" "/DSourceDir=$deploy" "$root/packaging/windows/x3ro-deck.iss"
$setup = "$root/packaging/windows/Output/x3ro-deck-$Version-setup.exe"
Sign @($setup)
(Get-FileHash $setup -Algorithm SHA256).Hash
