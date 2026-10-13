#preproc ispp

#define ProductName "World War VR Community"
#define ProductExe "WorldWarVR.exe"
#define ProductPublisher "World War VR"

#ifndef PayloadDir
  #error PayloadDir must point to the validated standalone payload directory.
#endif
#ifndef PayloadManifest
  #error PayloadManifest must point to the generated installer payload manifest.
#endif
#ifndef PayloadManifestSha256
  #error PayloadManifestSha256 must contain the generated manifest SHA-256.
#endif
#ifndef PayloadCount
  #error PayloadCount must contain the generated manifest file count.
#endif
#ifndef InstallerOutputDir
  #error InstallerOutputDir must point to the installer output directory.
#endif
#ifndef ProductVersion
  #define ProductVersion "0.4.0-alpha.2"
#endif
#ifndef ProductFileVersion
  #define ProductFileVersion "0.4.0.0"
#endif
#ifndef ProductAppId
  ; This fork-specific identity must remain stable across all future releases.
  #define ProductAppId "{{910B3F3E-600D-41E6-A5EA-45E464B12C4B}"
#endif

[Setup]
AppId={#ProductAppId}
AppName={#ProductName}
AppVersion={#ProductVersion}
AppVerName={#ProductName} {#ProductVersion}
AppPublisher={#ProductPublisher}
VersionInfoVersion={#ProductFileVersion}
VersionInfoDescription={#ProductName} installer
VersionInfoProductName={#ProductName}
VersionInfoProductVersion={#ProductFileVersion}
SetupIconFile={#PayloadDir}\WorldWarVR.ico
; Keep this fork isolated from the original project's installer registration
; and install directory. Two uninstallers must never own the same files.
DefaultDirName={localappdata}\Programs\World War VR Community
DisableDirPage=yes
DefaultGroupName={#ProductName}
DisableProgramGroupPage=yes
UninstallDisplayName={#ProductName}
UninstallDisplayIcon={app}\{#ProductExe}
OutputDir={#InstallerOutputDir}
OutputBaseFilename=WorldWarVR-v{#ProductVersion}-Setup
LicenseFile={#PayloadDir}\LICENSE
InfoBeforeFile={#PayloadDir}\INSTALL.txt
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
CloseApplications=yes
RestartApplications=no
RestartIfNeededByRun=no
SetupLogging=yes
UsePreviousAppDir=yes
UsePreviousGroup=no
DirExistsWarning=no
DisableWelcomePage=no
DisableReadyPage=no
AllowNoIcons=yes
ChangesAssociations=no
ChangesEnvironment=no
Uninstallable=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
; Keep the temporary manifest first so solid compression does not require
; decompressing the whole payload before PrepareToInstall can verify it.
Source: "{#PayloadManifest}"; Flags: dontcopy
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\World War VR Community"; Filename: "{app}\{#ProductExe}"; WorkingDir: "{app}"; Comment: "Launch Call of Duty: World at War in VR"
Name: "{group}\Uninstall World War VR Community"; Filename: "{uninstallexe}"
Name: "{userdesktop}\World War VR Community"; Filename: "{app}\{#ProductExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#ProductExe}"; Description: "Launch World War VR"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
const
  PayloadManifestName = 'payload-manifest.txt';

function NormalizedRelativePath(Value: String): String;
begin
  Value := Trim(Value);
  StringChangeEx(Value, '/', '\', True);
  Result := Value;
end;

function IsSafeRelativePath(const Value: String): Boolean;
var
  Padded: String;
begin
  Result := False;
  if (Value = '') or (Value[1] = '\') or
     (Pos(':', Value) <> 0) or (Pos('\\', Value) <> 0) then
    Exit;

  Padded := '\' + Lowercase(Value) + '\';
  if (Pos('\..\', Padded) <> 0) or (Pos('\.\', Padded) <> 0) then
    Exit;

  Result := True;
end;

function ReadPathList(
  const FileName: String;
  var Values: TArrayOfString): Boolean;
var
  Loaded: TArrayOfString;
  Index: Integer;
  Value: String;
begin
  SetArrayLength(Values, 0);
  if not FileExists(FileName) then
  begin
    Result := True;
    Exit;
  end;

  if not LoadStringsFromFile(FileName, Loaded) then
  begin
    Result := False;
    Exit;
  end;

  for Index := 0 to GetArrayLength(Loaded) - 1 do
  begin
    Value := NormalizedRelativePath(Loaded[Index]);
    if Value <> '' then
    begin
      SetArrayLength(Values, GetArrayLength(Values) + 1);
      Values[GetArrayLength(Values) - 1] := Value;
    end;
  end;
  Result := True;
end;

function ValidatePathList(
  const Values: TArrayOfString;
  const Description: String;
  var Reason: String): Boolean;
var
  Index: Integer;
begin
  Result := False;
  for Index := 0 to GetArrayLength(Values) - 1 do
  begin
    if not IsSafeRelativePath(Values[Index]) then
    begin
      Reason := Description + ' contains an unsafe path: ' + Values[Index];
      Exit;
    end;
  end;
  Result := True;
end;

function ValidateEmbeddedPayloadManifest: String;
var
  ManifestPath: String;
  PayloadFiles: TArrayOfString;
begin
  Result := '';
  ManifestPath := ExpandConstant('{tmp}\') + PayloadManifestName;
  try
    ExtractTemporaryFile(PayloadManifestName);
  except
    Result := 'Setup could not extract its payload manifest.';
    Exit;
  end;

  if CompareText(
      GetSHA256OfFile(ManifestPath), '{#PayloadManifestSha256}') <> 0 then
  begin
    Result := 'The embedded payload manifest failed its SHA-256 check. ' +
      'Download Setup again.';
    Exit;
  end;

  if not ReadPathList(ManifestPath, PayloadFiles) then
  begin
    Result := 'Setup could not read its payload manifest.';
    Exit;
  end;
  if GetArrayLength(PayloadFiles) <> {#PayloadCount} then
  begin
    Result := 'The payload manifest has an unexpected file count.';
    Exit;
  end;
  if not ValidatePathList(
      PayloadFiles, 'The payload manifest', Result) then
    Exit;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  NeedsRestart := False;
  Result := ValidateEmbeddedPayloadManifest;
end;
