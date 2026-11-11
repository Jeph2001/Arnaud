; Inno Setup script: turns dist\PharmacyMS (made by `make dist`) into a single
; PharmacyMS-Setup.exe installer. Open in Inno Setup 6 and press Compile,
; or run:  iscc tools\installer.iss
[Setup]
AppName=Pharmacy Management System
AppVersion=1.0
DefaultDirName={autopf}\PharmacyMS
DefaultGroupName=Pharmacy Management System
OutputDir=..\dist
OutputBaseFilename=PharmacyMS-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog

[Files]
Source: "..\dist\PharmacyMS\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\Pharmacy Management System"; Filename: "{app}\pharmacy.exe"
Name: "{autodesktop}\Pharmacy Management System"; Filename: "{app}\pharmacy.exe"; Tasks: desktopicon

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"

[Run]
Filename: "{app}\pharmacy.exe"; Description: "Launch Pharmacy Management System"; Flags: nowait postinstall skipifsilent
