; Airloom's Windows installer (Inno Setup 6: https://jrsoftware.org/isinfo.php).
; package.ps1 builds it; by hand:  ISCC /DAppVersion=1.0.0 /DExe=<path to Airloom.exe> Packaging\Airloom.iss
;
; It installs just for the current user by default (no administrator prompt), into
; %LOCALAPPDATA%\Programs\Airloom, with a Start menu entry, an optional desktop shortcut and
; .liverig / .liverigpack files opening in Airloom. Uninstalling leaves sets, recordings and
; settings (%APPDATA%\LiveRig, Music\Airloom Sets) alone.
;
; Signing: with /DSign the installer and uninstaller are signed through a sign tool named "airloom"
; (ISCC /Sairloom="signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 $f").
; Airloom.exe itself is signed before it gets here (package.ps1 -Sign).

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef Exe
  #define Exe "..\Release\Airloom\Airloom.exe"
#endif

[Setup]
AppId={{EF731F3D-FC1B-4A37-8828-574899089C06}
AppName=Airloom
AppVersion={#AppVersion}
AppVerName=Airloom {#AppVersion}
AppPublisher=Airloom
AppComments=An instrument for live, looping, ambient performance
DefaultDirName={autopf}\Airloom
DefaultGroupName=Airloom
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no
SetupIconFile=Airloom.ico
UninstallDisplayIcon={app}\Airloom.exe
UninstallDisplayName=Airloom
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir=..\Release
OutputBaseFilename=Airloom-Setup-{#AppVersion}
VersionInfoVersion={#AppVersion}
VersionInfoProductName=Airloom
#ifdef Sign
SignTool=airloom
SignedUninstaller=yes
#endif

[Tasks]
Name: "desktopicon"; Description: "Put a shortcut on the desktop"
Name: "associate"; Description: "Open Airloom sets (.liverig, .liverigpack) with Airloom"

[Files]
Source: "{#Exe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Airloom"; Filename: "{app}\Airloom.exe"
Name: "{autodesktop}\Airloom"; Filename: "{app}\Airloom.exe"; Tasks: desktopicon

[Registry]
; HKA: the current user's classes for a just-me install, the machine's for all users.
Root: HKA; Subkey: "Software\Classes\.liverig"; ValueType: string; ValueName: ""; ValueData: "Airloom.Set"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Airloom.Set"; ValueType: string; ValueName: ""; ValueData: "Airloom set"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Airloom.Set\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Airloom.exe,0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Airloom.Set\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Airloom.exe"" ""%1"""; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.liverigpack"; ValueType: string; ValueName: ""; ValueData: "Airloom.Pack"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Airloom.Pack"; ValueType: string; ValueName: ""; ValueData: "Airloom set pack"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Airloom.Pack\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Airloom.exe,0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Airloom.Pack\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Airloom.exe"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\Airloom.exe"; Description: "Open Airloom now"; Flags: nowait postinstall skipifsilent
