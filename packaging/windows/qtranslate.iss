; qtranslate installer (Inno Setup 6.3+ / 7).
;
; The installer is per-user and does not require administrator rights:
; files go to %LOCALAPPDATA%\Programs\qtranslate, shortcuts and the
; uninstall entry are created for the current user only. The portable ZIP
; remains the option for users who do not want to install anything.
;
; Build with packaging/windows/package.ps1, which passes all defines below.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppVersionQuad
  #define AppVersionQuad "0.0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build\dist"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\build\artifacts"
#endif

[Setup]
AppId={{937CC1A0-7C30-48A5-8838-E78AE17B706E}
AppName=qtranslate
AppVersion={#AppVersion}
AppVerName=qtranslate {#AppVersion}
AppPublisher=youkaicat
AppPublisherURL=https://github.com/youkaicat/qtranslate
AppSupportURL=https://github.com/youkaicat/qtranslate/issues
AppUpdatesURL=https://github.com/youkaicat/qtranslate/releases
DefaultDirName={localappdata}\Programs\qtranslate
DefaultGroupName=qtranslate
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir={#OutputDir}
OutputBaseFilename=qtranslate-{#AppVersion}-win64-setup
SetupIconFile=..\..\resources\icons\app.ico
UninstallDisplayIcon={app}\bin\qtranslate.exe
UninstallDisplayName=qtranslate {#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
CloseApplications=yes
RestartApplications=no
AppMutex=Local\qtranslate-app
VersionInfoVersion={#AppVersionQuad}
VersionInfoProductVersion={#AppVersion}
VersionInfoProductName=qtranslate
VersionInfoDescription=qtranslate Setup
VersionInfoCompany=youkaicat

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\qtranslate"; Filename: "{app}\bin\qtranslate.exe"; WorkingDir: "{app}\bin"; Comment: "Fast desktop translator"
Name: "{group}\{cm:UninstallProgram,qtranslate}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\qtranslate"; Filename: "{app}\bin\qtranslate.exe"; WorkingDir: "{app}\bin"; Comment: "Fast desktop translator"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\qtranslate.exe"; Description: "Launch qtranslate"; Flags: nowait postinstall skipifsilent
