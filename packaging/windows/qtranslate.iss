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
Name: "startmenuicon"; Description: "{cm:CreateStartMenuIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "{cm:AutoStartProgram,qtranslate}"; GroupDescription: "{cm:AutoStartProgramGroupDescription}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\qtranslate"; Filename: "{app}\bin\qtranslate.exe"; WorkingDir: "{app}\bin"; Comment: "Fast desktop translator"; Tasks: startmenuicon
Name: "{group}\{cm:UninstallProgram,qtranslate}"; Filename: "{uninstallexe}"; Tasks: startmenuicon
Name: "{autodesktop}\qtranslate"; Filename: "{app}\bin\qtranslate.exe"; WorkingDir: "{app}\bin"; Comment: "Fast desktop translator"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\qtranslate.exe"; Description: "Launch qtranslate"; Flags: nowait postinstall skipifsilent

[CustomMessages]
english.CreateStartMenuIcon=Create a &Start menu shortcut
russian.CreateStartMenuIcon=Создать ярлык в меню «Пуск»

[Code]
var
  AutostartTaskInitialized: Boolean;
  AutostartEnabled: Boolean;

function SettingsFile: String;
begin
  Result := ExpandConstant('{userappdata}\qtranslate\qtranslate.ini');
end;

function StoredAutostart: Boolean;
var
  S: String;
begin
  S := GetIniString('app', 'autostart', 'true', SettingsFile);
  Result := (S <> '0') and (Uppercase(S) <> 'FALSE') and (Uppercase(S) <> 'NO');
end;

function CommandLineHasTasksParam: Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if Pos('/TASKS', Uppercase(ParamStr(I))) = 1 then
    begin
      Result := True;
      Exit;
    end;
end;

function CommandLineSelectsAutostart: Boolean;
var
  I: Integer;
  S: String;
begin
  Result := False;
  for I := 1 to ParamCount do
  begin
    S := ParamStr(I);
    if Uppercase(Copy(S, 1, 7)) = '/TASKS=' then
    begin
      Result := Pos('autostart', Lowercase(S)) > 0;
      Exit;
    end;
  end;
end;

function FindAutostartItemIndex: Integer;
var
  I: Integer;
begin
  Result := -1;
  for I := 0 to WizardForm.TasksList.Items.Count - 1 do
    if Pos('qtranslate', WizardForm.TasksList.ItemCaption[I]) > 0 then
    begin
      Result := I;
      Exit;
    end;
end;

procedure ApplyStoredAutostartTask;
var
  Index: Integer;
begin
  if AutostartTaskInitialized then
    Exit;

  Index := FindAutostartItemIndex;
  if Index >= 0 then
  begin
    AutostartEnabled := StoredAutostart;
    WizardForm.TasksList.Checked[Index] := AutostartEnabled;
    AutostartTaskInitialized := True;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpSelectTasks) and (not CommandLineHasTasksParam) then
    ApplyStoredAutostartTask;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  if (not WizardSilent) and (CurPageID = wpSelectTasks) then
    AutostartEnabled := WizardIsTaskSelected('autostart');
  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Value: String;
  RunKey: String;
  Command: String;
begin
  if CurStep = ssInstall then
  begin
    if WizardSilent then
    begin
      if CommandLineHasTasksParam then
        AutostartEnabled := CommandLineSelectsAutostart
      else
        AutostartEnabled := StoredAutostart;
    end;
  end;

  if CurStep <> ssPostInstall then
    Exit;

  if AutostartEnabled then
    Value := 'true'
  else
    Value := 'false';

  ForceDirectories(ExtractFileDir(SettingsFile));
  SetIniString('app', 'autostart', Value, SettingsFile);

  RunKey := 'Software\Microsoft\Windows\CurrentVersion\Run';
  if AutostartEnabled then
  begin
    Command := '"' + ExpandConstant('{app}\bin\qtranslate.exe') + '" --minimized';
    RegWriteStringValue(HKCU, RunKey, 'qtranslate', Command);
  end
  else
    RegDeleteValue(HKCU, RunKey, 'qtranslate');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'qtranslate');
end;
