; Claude Box installer
; Build: iscc gui\packaging\claude-box-gui.iss  (from repo root)
;
; Prerequisites installed on demand (download at install time):
;   - Visual C++ 2022 Redistributable x64
;   - Docker Desktop
;
; Also enables Docker Desktop auto-start at Windows login if it was not
; already configured (the most common reason the app can't reach Docker
; on a fresh install).

#define AppName      "Claude Box"
#define AppVersion   "3.1.0"
#define AppPublisher "Isaac Morris"
#define AppExeName   "claude-box-gui.exe"
#define BuildDir     "..\build\Release"

[Setup]
AppId={{A3F2C1D4-7B8E-4F5A-9C2D-1E6B3A4F7D8C}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/morris-lab-experiments/claude-box
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
AllowNoIcons=yes
OutputDir=..\..\installer
OutputBaseFilename=claude-box-setup-{#AppVersion}
SetupIconFile=claude-box.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
; Docker's auto-start Run key is correctly HKCU (per-user).  We know we're
; running as admin and deliberately writing the Run key for this user.
UsedUserAreasWarning=no
ArchitecturesInstallIn64BitMode=x64compatible
; Minimum Windows 10
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon";    Description: "Create a &desktop shortcut";              GroupDescription: "Additional shortcuts:"
Name: "dockerautostart"; Description: "Start Docker Desktop &automatically at login"; GroupDescription: "Docker Desktop:"

[Files]
; App executable
Source: "{#BuildDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion

; Qt runtime DLLs
Source: "{#BuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion

; Qt plugin subdirectories (windeployqt output)
Source: "{#BuildDir}\generic\*";           DestDir: "{app}\generic";           Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\iconengines\*";       DestDir: "{app}\iconengines";       Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\imageformats\*";      DestDir: "{app}\imageformats";      Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\platforms\*";         DestDir: "{app}\platforms";         Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\styles\*";            DestDir: "{app}\styles";            Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\tls\*";               DestDir: "{app}\tls";               Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\translations\*";      DestDir: "{app}\translations";      Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}";             Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\{#AppExeName}"
Name: "{group}\Uninstall {#AppName}";   Filename: "{uninstallexe}"
Name: "{commondesktop}\{#AppName}";     Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\{#AppExeName}"; Tasks: desktopicon

; Docker Desktop auto-start: HKCU so it applies to the installing user.
; Docker Desktop itself sets this same key when "Start Docker Desktop when
; you log in" is enabled in its settings UI.
[Registry]
Root: HKCU; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "Docker Desktop"; \
  ValueData: """{autopf}\Docker\Docker\Docker Desktop.exe"""; \
  Tasks: dockerautostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; \
  Flags: nowait postinstall skipifsilent

[Code]

// -------------------------------------------------------------------------
// Prerequisite helpers
// -------------------------------------------------------------------------

// VC++ 2022 x64 Redistributable: installer sets HKLM\...\Runtimes\x64\Installed=1
function VCRedistInstalled: Boolean;
var
  Installed: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
    'Installed', Installed) and (Installed = 1);
end;

function DockerInstalled: Boolean;
begin
  Result := FileExists(ExpandConstant('{autopf}\Docker\Docker\Docker Desktop.exe'));
end;

// -------------------------------------------------------------------------
// Download page (shown on "Next" from the Ready page when prereqs are needed)
// -------------------------------------------------------------------------

var
  DownloadPage: TDownloadWizardPage;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage(
    'Downloading prerequisites',
    'Please wait while the installer downloads required components.',
    nil);
end;

function NeedDownloads: Boolean;
begin
  Result := (not VCRedistInstalled) or (not DockerInstalled);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpReady) and NeedDownloads then begin
    DownloadPage.Clear;
    if not VCRedistInstalled then
      DownloadPage.Add(
        'https://aka.ms/vs/17/release/vc_redist.x64.exe',
        'vc_redist.x64.exe', '');
    if not DockerInstalled then
      DownloadPage.Add(
        'https://desktop.docker.com/win/main/amd64/Docker%20Desktop%20Installer.exe',
        'DockerDesktopInstaller.exe', '');
    DownloadPage.Show;
    try
      try
        DownloadPage.Download;
      except
        SuppressibleMsgBox(
          'Download failed: ' + GetExceptionMessage + #13#10#13#10 +
          'Please check your internet connection and try again.',
          mbCriticalError, MB_OK, IDOK);
        Result := False;
      end;
    finally
      DownloadPage.Hide;
    end;
  end;
end;

// -------------------------------------------------------------------------
// Install prereqs (runs before files are copied)
// -------------------------------------------------------------------------

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
  VCPath, DockerPath: String;
begin
  Result := '';

  if not VCRedistInstalled then begin
    VCPath := ExpandConstant('{tmp}\vc_redist.x64.exe');
    if not Exec(VCPath, '/install /quiet /norestart', '',
                SW_HIDE, ewWaitUntilTerminated, ResultCode) then begin
      Result := 'Failed to install Visual C++ 2022 Redistributable (error ' +
                IntToStr(ResultCode) + ').';
      Exit;
    end;
    if ResultCode = 3010 then  // ERROR_SUCCESS_REBOOT_REQUIRED
      NeedsRestart := True;
  end;

  if not DockerInstalled then begin
    DockerPath := ExpandConstant('{tmp}\DockerDesktopInstaller.exe');
    // Docker Desktop installer: "install" verb, quiet, accept license
    if not Exec(DockerPath, 'install --quiet --accept-license', '',
                SW_SHOW, ewWaitUntilTerminated, ResultCode) then begin
      Result := 'Failed to install Docker Desktop (error ' +
                IntToStr(ResultCode) + ').';
      Exit;
    end;
    // Docker Desktop requires a logout/restart to fully initialise WSL2
    if ResultCode = 0 then
      NeedsRestart := True;
  end;
end;
