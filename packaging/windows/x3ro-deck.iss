; SPDX-FileCopyrightText: 2026 x3ro
; SPDX-License-Identifier: GPL-3.0-or-later
;
; Inno Setup script: a per-user install of the windeployqt'd build in
; {#SourceDir}. scripts/windows-package.ps1 runs it.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build-release-windows\deploy"
#endif

[Setup]
AppId={{6F2B7C1E-3D4A-4E9B-9C51-8A0D2E7F4B13}
AppName=x3ro deck
AppVersion={#AppVersion}
AppPublisher=x3ro
AppPublisherURL=https://github.com/djhype/x3ro-deck
DefaultDirName={localappdata}\Programs\x3ro-deck
DefaultGroupName=x3ro deck
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputBaseFilename=x3ro-deck-{#AppVersion}-setup
SetupIconFile=..\..\data\icons\x3ro-deck.ico
UninstallDisplayIcon={app}\x3ro-deck.exe
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}\licenses"
Source: "..\..\TRADEMARKS.md"; DestDir: "{app}\licenses"
Source: "..\..\data\licenses\*"; DestDir: "{app}\licenses"

[Icons]
; The AppUserModelID must match the app's, or Windows drops its toasts.
Name: "{userprograms}\x3ro deck"; Filename: "{app}\x3ro-deck.exe"; AppUserModelID: "life.x3ro.deck"

[Run]
Filename: "{app}\x3ro-deck.exe"; Description: "Start x3ro deck"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Agents run in the session daemon; stop it and the app before removing files.
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM x3ro-deck.exe"; Flags: runhidden; RunOnceId: "StopApp"
Filename: "{sys}\taskkill.exe"; Parameters: "/F /T /IM x3ro-deck-sessiond.exe"; Flags: runhidden; RunOnceId: "StopDaemon"
