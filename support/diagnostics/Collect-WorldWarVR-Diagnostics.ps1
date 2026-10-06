[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        'IndexSteamVR',
        'Pimax',
        'FlatLaunch',
        'GermanUncut',
        'CustomZombies',
        'RandomMelee',
        'YangYoungRegression',
        'PauseMenu')]
    [string]$Issue,

    [string]$GameDirectory = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$packageRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$resultName = "WorldWarVR-$Issue-Diagnostics-$timestamp"
$resultDirectory = Join-Path $packageRoot $resultName
$resultZip = "$resultDirectory.zip"
$reportPath = Join-Path $resultDirectory 'SUPPORT-REPORT.txt'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

if (Get-Process -Name CoDWaW,WorldWarVR,wawvr-launcher -ErrorAction SilentlyContinue) {
    throw 'Close World at War and the World War VR launcher before collecting diagnostics.'
}
if ((Test-Path -LiteralPath $resultDirectory) -or
    (Test-Path -LiteralPath $resultZip)) {
    throw "Diagnostic output already exists: $resultName"
}
New-Item -ItemType Directory -Path $resultDirectory | Out-Null

$report = [System.Collections.Generic.List[string]]::new()
function Add-ReportSection {
    param([string]$Title)
    $report.Add('')
    $report.Add("=== $Title ===")
}
function Add-ReportLine {
    param([string]$Text)
    $report.Add($Text)
}
function Safe-Hash {
    param([string]$Path)
    $stream = $null
    $sha256 = $null
    try {
        $stream = [System.IO.File]::Open(
            $Path,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        $bytes = $sha256.ComputeHash($stream)
        return ([System.BitConverter]::ToString($bytes)).Replace('-', '')
    }
    catch {
        return "unavailable: $($_.Exception.Message)"
    }
    finally {
        if ($null -ne $sha256) {
            $sha256.Dispose()
        }
        if ($null -ne $stream) {
            $stream.Dispose()
        }
    }
}
function File-Summary {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return '(empty)'
    }
    try {
        $fullPath = [System.IO.Path]::GetFullPath(
            [Environment]::ExpandEnvironmentVariables($Path.Trim().Trim('"')))
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            return "$fullPath | missing"
        }
        $item = Get-Item -LiteralPath $fullPath
        $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($fullPath)
        return "$fullPath | bytes=$($item.Length) | version=$($version.FileVersion) | sha256=$(Safe-Hash $fullPath)"
    }
    catch {
        return "$Path | inspection failed: $($_.Exception.Message)"
    }
}
function Read-OpenXrRegistryView {
    param(
        [Microsoft.Win32.RegistryHive]$Hive,
        [Microsoft.Win32.RegistryView]$View,
        [string]$Label
    )
    try {
        $root = [Microsoft.Win32.RegistryKey]::OpenBaseKey($Hive, $View)
        try {
            $key = $root.OpenSubKey('SOFTWARE\Khronos\OpenXR\1')
            if ($null -eq $key) {
                Add-ReportLine "${Label}: OpenXR key missing"
                return
            }
            try {
                $active = $key.GetValue('ActiveRuntime')
                Add-ReportLine "$Label ActiveRuntime: $active"
                if ($active -is [string]) {
                    $script:runtimeManifestCandidates.Add($active)
                }
                $available = $key.OpenSubKey('AvailableRuntimes')
                if ($null -eq $available) {
                    Add-ReportLine "$Label AvailableRuntimes: key missing"
                }
                else {
                    try {
                        $names = @($available.GetValueNames())
                        Add-ReportLine "$Label AvailableRuntimes count: $($names.Count)"
                        foreach ($name in $names) {
                            Add-ReportLine "  $name = $($available.GetValue($name))"
                            if (-not [string]::IsNullOrWhiteSpace($name)) {
                                $script:runtimeManifestCandidates.Add($name)
                            }
                        }
                    }
                    finally {
                        $available.Dispose()
                    }
                }
            }
            finally {
                $key.Dispose()
            }
        }
        finally {
            $root.Dispose()
        }
    }
    catch {
        Add-ReportLine "$Label registry read failed: $($_.Exception.Message)"
    }
}
function Inspect-RuntimeManifest {
    param([string]$ManifestPath)
    if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
        return
    }
    try {
        $expanded = [Environment]::ExpandEnvironmentVariables(
            $ManifestPath.Trim().Trim('"'))
        $fullManifest = [System.IO.Path]::GetFullPath($expanded)
        Add-ReportLine "Manifest: $(File-Summary $fullManifest)"
        if (-not (Test-Path -LiteralPath $fullManifest -PathType Leaf)) {
            return
        }
        $json = Get-Content -LiteralPath $fullManifest -Raw | ConvertFrom-Json
        $library = [string]$json.runtime.library_path
        if ([string]::IsNullOrWhiteSpace($library)) {
            Add-ReportLine '  runtime.library_path: missing'
            return
        }
        $library = [Environment]::ExpandEnvironmentVariables($library)
        if (-not [System.IO.Path]::IsPathRooted($library)) {
            $library = Join-Path ([System.IO.Path]::GetDirectoryName($fullManifest)) $library
        }
        Add-ReportLine "  Runtime library: $(File-Summary $library)"
    }
    catch {
        Add-ReportLine "Manifest inspection failed for ${ManifestPath}: $($_.Exception.Message)"
    }
}

Add-ReportLine 'World War VR customer support diagnostic'
Add-ReportLine "Package issue: $Issue"
Add-ReportLine "Collected: $(Get-Date -Format o)"
Add-ReportLine "Package root: $packageRoot"
Add-ReportLine "PowerShell: $($PSVersionTable.PSVersion)"
Add-ReportLine "Windows: $([Environment]::OSVersion.VersionString)"
Add-ReportLine "64-bit OS: $([Environment]::Is64BitOperatingSystem)"
Add-ReportLine "64-bit collector: $([Environment]::Is64BitProcess)"

Add-ReportSection 'Package files'
foreach ($name in @('WorldWarVR.exe', 'wawvr-launcher.exe', 'wawvr-launcher-cli.exe', 'WorldWarVR.dll', 'BUILD-INFO.txt')) {
    Add-ReportLine (File-Summary (Join-Path $packageRoot $name))
}

Add-ReportSection 'Display adapters'
try {
    foreach ($gpu in @(Get-CimInstance Win32_VideoController)) {
        Add-ReportLine "$($gpu.Name) | driver=$($gpu.DriverVersion) | status=$($gpu.Status)"
    }
}
catch {
    Add-ReportLine "GPU inventory failed: $($_.Exception.Message)"
}

Add-ReportSection 'Selected VR processes'
$vrProcessNames = @('vrserver', 'vrmonitor', 'vrcompositor', 'OVRServer_x64', 'PimaxClient', 'pi_server', 'VirtualDesktop.Streamer')
foreach ($name in $vrProcessNames) {
    $instances = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
    Add-ReportLine "${name}: $($instances.Count) running"
}

Add-ReportSection 'Environment'
foreach ($name in @(
    'XR_RUNTIME_JSON',
    'WAWVR_OPENXR_RUNTIME_SELECTION',
    'WAWVR_SUPPORT_DIAGNOSTIC_KIND',
    'WAWVR_FRAME_TIMING_DIAGNOSTICS',
    'WAWVR_ORIENTATION_DIAGNOSTICS',
    'WAWVR_MOSIN_BULLET_DIAGNOSTICS',
    'WAWVR_POST_T4_VISIBLE_AIM_DIAGNOSTICS',
    'WAWVR_WEAPON_PIPELINE_TRACE',
    'WAWVR_FX_STEREO_DIAGNOSTICS',
    'OculusBase')) {
    Add-ReportLine "$name=$([Environment]::GetEnvironmentVariable($name))"
}

$script:runtimeManifestCandidates = [System.Collections.Generic.List[string]]::new()
Add-ReportSection 'OpenXR registry'
Read-OpenXrRegistryView LocalMachine Registry32 'HKLM 32-bit'
Read-OpenXrRegistryView CurrentUser Registry32 'HKCU 32-bit'
Read-OpenXrRegistryView LocalMachine Registry64 'HKLM 64-bit'
Read-OpenXrRegistryView CurrentUser Registry64 'HKCU 64-bit'
$explicitRuntime = [Environment]::GetEnvironmentVariable('XR_RUNTIME_JSON')
if (-not [string]::IsNullOrWhiteSpace($explicitRuntime)) {
    $script:runtimeManifestCandidates.Add($explicitRuntime)
}
foreach ($root in @(
    $env:ProgramW6432,
    $env:ProgramFiles,
    ${env:ProgramFiles(x86)}
)) {
    if (-not [string]::IsNullOrWhiteSpace($root)) {
        $script:runtimeManifestCandidates.Add((Join-Path $root 'Pimax\Runtime\PiOpenXR_32.json'))
    }
}

Add-ReportSection 'OpenXR manifests and libraries'
foreach ($candidate in @($script:runtimeManifestCandidates | Select-Object -Unique)) {
    Inspect-RuntimeManifest $candidate
}

$settingsPath = Join-Path $env:LOCALAPPDATA 'WorldAtWarVR\launcher-settings.json'
$settings = $null
Add-ReportSection 'Launcher settings'
if (Test-Path -LiteralPath $settingsPath -PathType Leaf) {
    Copy-Item -LiteralPath $settingsPath -Destination (
        Join-Path $resultDirectory 'launcher-settings.json')
    try {
        $settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
        Add-ReportLine "Settings copied from: $settingsPath"
        Add-ReportLine "GameDirectory=$($settings.GameDirectory)"
        Add-ReportLine "LaunchTarget=$($settings.LaunchTarget)"
        Add-ReportLine "QuestAirLinkCompatibility=$($settings.QuestAirLinkCompatibility)"
        foreach ($propertyName in @(
            'Resolution',
            'TurnMode',
            'AutomaticReload',
            'ButtonGrenades',
            'DetectedLanguage')) {
            $property = $settings.PSObject.Properties[$propertyName]
            if ($null -ne $property) {
                Add-ReportLine "$propertyName=$($property.Value)"
            }
        }
    }
    catch {
        Add-ReportLine "Settings parse failed: $($_.Exception.Message)"
    }
}
else {
    Add-ReportLine "Settings file missing: $settingsPath"
}

if ([string]::IsNullOrWhiteSpace($GameDirectory) -and $null -ne $settings) {
    $GameDirectory = [string]$settings.GameDirectory
}
if ([string]::IsNullOrWhiteSpace($GameDirectory) -and $Issue -eq 'GermanUncut') {
    $GameDirectory = Read-Host 'Paste the Call of Duty World at War installation folder'
}

Add-ReportSection 'Game executable identity'
if ([string]::IsNullOrWhiteSpace($GameDirectory)) {
    Add-ReportLine 'Game directory unavailable.'
}
else {
    try {
        $GameDirectory = [System.IO.Path]::GetFullPath($GameDirectory.Trim().Trim('"'))
        Add-ReportLine "Game directory: $GameDirectory"
        foreach ($name in @('CoDWaW.exe', 't4sp.exe', 'CoDWaWmp.exe', 't4mp.exe', 'localization.txt')) {
            Add-ReportLine (File-Summary (Join-Path $GameDirectory $name))
        }
        $zoneRoot = Join-Path $GameDirectory 'zone'
        if (Test-Path -LiteralPath $zoneRoot -PathType Container) {
            $languages = @(Get-ChildItem -LiteralPath $zoneRoot -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name | Sort-Object)
            Add-ReportLine "Zone language directories: $($languages -join ', ')"
        }
    }
    catch {
        Add-ReportLine "Game-directory inspection failed: $($_.Exception.Message)"
    }
}

Add-ReportSection 'Read-only launcher diagnostic'
$cli = Join-Path $packageRoot 'wawvr-launcher-cli.exe'
$modDll = Join-Path $packageRoot 'WorldWarVR.dll'
if ((Test-Path -LiteralPath $cli -PathType Leaf) -and
    -not [string]::IsNullOrWhiteSpace($GameDirectory)) {
    try {
        $arguments = @('--diagnose', '--game-dir', $GameDirectory, '--mod-dll', $modDll)
        $diagnosticOutput = & $cli @arguments 2>&1 | Out-String
        Add-ReportLine $diagnosticOutput.TrimEnd()
        Add-ReportLine "Diagnostic exit code: $LASTEXITCODE"
    }
    catch {
        Add-ReportLine "Launcher diagnostic failed: $($_.Exception.Message)"
    }
}
else {
    Add-ReportLine 'Launcher diagnostic skipped because the CLI or game directory was unavailable.'
}

Add-ReportSection 'Selected isolated-profile renderer settings'
$launcherProfileRoot = Join-Path $env:LOCALAPPDATA 'WorldAtWarVR'
$isolatedDataRoot = Join-Path $env:LOCALAPPDATA 'WaWVR'
$profileRoots = [System.Collections.Generic.List[string]]::new()
$profileRoots.Add($launcherProfileRoot)
if (Test-Path -LiteralPath $isolatedDataRoot -PathType Container) {
    foreach ($profileDirectory in @(Get-ChildItem -LiteralPath $isolatedDataRoot -Directory -Filter 'home*' -ErrorAction SilentlyContinue)) {
        $profileRoots.Add($profileDirectory.FullName)
    }
}
$configPattern = '^(?:seta\s+)?(?:r_fullscreen|r_mode|r_customMode|fs_game|com_introPlayed|vid_xpos|vid_ypos|cg_bobWeaponAmplitude|bg_bobMax)\b'
foreach ($profileRoot in @($profileRoots | Select-Object -Unique)) {
    if (Test-Path -LiteralPath $profileRoot -PathType Container) {
        foreach ($config in @(Get-ChildItem -LiteralPath $profileRoot -Filter config.cfg -File -Recurse -ErrorAction SilentlyContinue)) {
            Add-ReportLine "Config: $($config.FullName)"
            foreach ($line in @(Get-Content -LiteralPath $config.FullName -ErrorAction SilentlyContinue | Where-Object { $_ -match $configPattern })) {
                Add-ReportLine "  $line"
            }
        }
    }
    else {
        Add-ReportLine "Profile root missing: $profileRoot"
    }
}

Add-ReportSection 'Custom-map inventory'
$modRoots = [System.Collections.Generic.List[string]]::new()
foreach ($candidate in @(
    (Join-Path $env:LOCALAPPDATA 'WaWVR\home\mods'),
    (Join-Path $env:LOCALAPPDATA 'Activision\CoDWaW\mods'),
    $(if (-not [string]::IsNullOrWhiteSpace($GameDirectory)) {
        Join-Path $GameDirectory 'mods'
    }))) {
    if (-not [string]::IsNullOrWhiteSpace($candidate)) {
        $modRoots.Add($candidate)
    }
}
if (Test-Path -LiteralPath $isolatedDataRoot -PathType Container) {
    foreach ($profileDirectory in @(Get-ChildItem -LiteralPath $isolatedDataRoot -Directory -Filter 'home*' -ErrorAction SilentlyContinue)) {
        $modRoots.Add((Join-Path $profileDirectory.FullName 'mods'))
    }
}
foreach ($modRoot in @($modRoots | Select-Object -Unique)) {
    Add-ReportLine "Mods root: $modRoot"
    if (-not (Test-Path -LiteralPath $modRoot -PathType Container)) {
        Add-ReportLine '  missing'
        continue
    }
    foreach ($modDirectory in @(Get-ChildItem -LiteralPath $modRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name)) {
        $files = @(Get-ChildItem -LiteralPath $modDirectory.FullName -File -Recurse -ErrorAction SilentlyContinue)
        $totalBytes = ($files | Measure-Object -Property Length -Sum).Sum
        if ($null -eq $totalBytes) {
            $totalBytes = 0
        }
        Add-ReportLine "  $($modDirectory.Name) | files=$($files.Count) | bytes=$totalBytes"
        foreach ($file in @($files | Where-Object {
            $_.Extension -in @('.ff', '.iwd', '.dll')
        } | Sort-Object FullName)) {
            $relative = $file.FullName.Substring($modDirectory.FullName.Length).TrimStart('\')
            Add-ReportLine "    $relative | bytes=$($file.Length) | modified=$($file.LastWriteTimeUtc.ToString('o'))"
        }
    }
}

Add-ReportSection 'Recent application failures'
try {
    $startTime = (Get-Date).AddDays(-7)
    $events = @(Get-WinEvent -FilterHashtable @{
        LogName = 'Application'
        Level = 2
        StartTime = $startTime
    } -ErrorAction Stop | Where-Object {
        $_.ProviderName -in @('Application Error', 'Windows Error Reporting') -and
        $_.Message -match '(?i)CoDWaW|WorldWarVR|WorldAtWarVR'
    } | Select-Object -First 20)
    if ($events.Count -eq 0) {
        Add-ReportLine 'No matching Application Error or Windows Error Reporting entries found.'
    }
    foreach ($event in $events) {
        $message = ($event.Message -replace "`r?`n", ' | ')
        Add-ReportLine "$($event.TimeCreated.ToString('o')) | provider=$($event.ProviderName) | id=$($event.Id) | $message"
    }
}
catch {
    Add-ReportLine "Application event-log query failed: $($_.Exception.Message)"
}

Add-ReportSection 'Collected log files'
$logCandidates = [System.Collections.Generic.List[string]]::new()
$logCandidates.Add((Join-Path $packageRoot 'WorldWarVR.log'))
$logCandidates.Add((Join-Path $launcherProfileRoot 'launcher-crash.log'))
foreach ($profileRoot in @($profileRoots | Select-Object -Unique)) {
    if (Test-Path -LiteralPath $profileRoot -PathType Container) {
        foreach ($log in @(Get-ChildItem -LiteralPath $profileRoot -Filter WorldWarVR.log -File -Recurse -ErrorAction SilentlyContinue)) {
            $logCandidates.Add($log.FullName)
        }
    }
}
$copiedLogIndex = 0
foreach ($candidate in @($logCandidates | Select-Object -Unique)) {
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        Add-ReportLine "Missing: $candidate"
        continue
    }
    ++$copiedLogIndex
    $destinationName = "log-$copiedLogIndex-$([System.IO.Path]::GetFileName($candidate))"
    Copy-Item -LiteralPath $candidate -Destination (
        Join-Path $resultDirectory $destinationName)
    Add-ReportLine "Copied: $candidate as $destinationName"
}

$orientationRoot = Join-Path $packageRoot 'support-orientation-captures'
if (Test-Path -LiteralPath $orientationRoot -PathType Container) {
    $orientationFiles = @(Get-ChildItem -LiteralPath $orientationRoot -File -ErrorAction SilentlyContinue)
    if ($orientationFiles.Count -gt 0) {
        $orientationDestination = Join-Path $resultDirectory 'orientation-captures'
        New-Item -ItemType Directory -Path $orientationDestination | Out-Null
        foreach ($file in $orientationFiles) {
            Copy-Item -LiteralPath $file.FullName -Destination $orientationDestination
        }
        Add-ReportLine "Copied $($orientationFiles.Count) orientation capture file(s)."
    }
}

$testNotes = Join-Path $packageRoot 'TEST-NOTES.txt'
if (Test-Path -LiteralPath $testNotes -PathType Leaf) {
    Copy-Item -LiteralPath $testNotes -Destination (
        Join-Path $resultDirectory 'TEST-NOTES.txt')
    Add-ReportLine 'Copied customer TEST-NOTES.txt.'
}

$privacy = @(
    'This diagnostic contains installation/runtime paths, hardware names, launcher settings, file metadata, and SHA-256 hashes.',
    'It does not copy a Call of Duty executable, map, fastfile, IWD, save, crash dump, password, authentication token, or account credential.',
    'For custom maps it records only mod folder/file names, sizes, and modification times.',
    'Review SUPPORT-REPORT.txt and launcher-settings.json before sending the ZIP.'
)
[System.IO.File]::WriteAllText(
    (Join-Path $resultDirectory 'PRIVACY.txt'),
    (($privacy -join "`r`n") + "`r`n"),
    $utf8NoBom)
[System.IO.File]::WriteAllText(
    $reportPath,
    (($report -join "`r`n") + "`r`n"),
    $utf8NoBom)

Compress-Archive -LiteralPath $resultDirectory -DestinationPath $resultZip -CompressionLevel Optimal
Write-Host ''
Write-Host "Diagnostic ZIP created: $resultZip"
Write-Host 'Review it, then send that ZIP to Jplay.'
Read-Host 'Press Enter to close this window'
