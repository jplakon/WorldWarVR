[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PayloadDir,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [ValidatePattern('^(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-(?:alpha|beta|rc)\.(?:0|[1-9][0-9]*))?$')]
    [string]$Version = '0.4.0-alpha.2',

    [ValidatePattern('^[A-Fa-f0-9]{8}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{12}$')]
    [string]$AppId = '910B3F3E-600D-41E6-A5EA-45E464B12C4B',

    [string]$InnoCompiler = '',

    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$ExpectedInnoCompilerSha256 = '0A8757031B33777E4C9CBFFEE40F11A5062B36D25CBE144C1DB73B6102B80AD7',

    [switch]$RequireSignature,
    [switch]$ValidateOnly,
    [switch]$PrintInventory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$setupScript = Join-Path $scriptRoot 'WorldWarVR.iss'
$resolvedPayload = [System.IO.Path]::GetFullPath($PayloadDir)
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDir)
$installerName = "WorldWarVR-v$Version-Setup.exe"
$installerPath = Join-Path $resolvedOutput $installerName
$sidecarPath = "$installerPath.sha256"
$null = $Version -match '^(\d+)\.(\d+)\.(\d+)'
$fileVersion = "$($Matches[1]).$($Matches[2]).$($Matches[3]).0"
foreach ($component in @($Matches[1], $Matches[2], $Matches[3])) {
    if ([uint64]$component -gt 65535) {
        throw "Version components must fit the Windows file-version range: $Version"
    }
}

function Assert-NormalDirectory {
    param(
        [Parameter(Mandatory = $true)] [string]$Path,
        [Parameter(Mandatory = $true)] [string]$Description
    )
    $item = Get-Item -LiteralPath $Path -Force
    if (-not $item.PSIsContainer) {
        throw "$Description is not a directory: $Path"
    }
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description must not be a junction or symbolic link: $Path"
    }
}

function Test-IsSameOrDescendant {
    param(
        [Parameter(Mandatory = $true)] [string]$Candidate,
        [Parameter(Mandatory = $true)] [string]$Root
    )
    $fullCandidate = [System.IO.Path]::GetFullPath($Candidate).TrimEnd('\')
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    return $fullCandidate.Equals($fullRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $fullCandidate.StartsWith(
            $fullRoot + '\',
            [StringComparison]::OrdinalIgnoreCase)
}

function Resolve-InnoCompiler {
    if (-not [string]::IsNullOrWhiteSpace($InnoCompiler)) {
        $explicitPath = [System.IO.Path]::GetFullPath($InnoCompiler)
        if (-not (Test-Path -LiteralPath $explicitPath -PathType Leaf)) {
            throw "Inno Setup compiler was not found: $explicitPath"
        }
        return $explicitPath
    }

    $command = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace(${env:ProgramFiles(x86)})) {
        $candidates += Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
    }
    if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
        $candidates += Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'
    }
    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        $candidates += Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }
    throw 'Inno Setup 6 was not found. Install the pinned Inno Setup 6.7.3 compiler or pass -InnoCompiler.'
}

if (-not (Test-Path -LiteralPath $setupScript -PathType Leaf)) {
    throw "Installer definition is missing: $setupScript"
}
if (-not (Test-Path -LiteralPath $resolvedPayload -PathType Container)) {
    throw "Payload directory was not found: $resolvedPayload"
}
Assert-NormalDirectory -Path $resolvedPayload -Description 'Payload directory'
if (Test-IsSameOrDescendant -Candidate $resolvedOutput -Root $resolvedPayload) {
    throw 'Installer output must remain outside the payload directory.'
}

$requiredTopLevelFiles = @(
    'WorldWarVR.exe',
    'WorldWarVR.Launcher.dll',
    'WorldWarVR.Launcher.Core.dll',
    'WorldWarVR.Launcher.deps.json',
    'WorldWarVR.Launcher.runtimeconfig.json',
    'WorldWarVR.pri',
    'WorldWarVR.ico',
    'wawvr-launcher.exe',
    'WorldWarVR.dll',
    'WaWVR-PeZBOT-Import.ps1',
    'LICENSE',
    'THIRD-PARTY-NOTICES.txt',
    'INSTALL.txt',
    'CONTROLS.txt',
    'KNOWN-ISSUES.txt',
    'REQUIREMENTS.txt',
    'OFFLINE-MULTIPLAYER.txt',
    'PROVENANCE.txt',
    'ASSET-PROVENANCE.txt',
    'CHANGELOG.txt',
    'SOURCE.txt',
    'BUILD-INFO.txt',
    'PAYLOAD-SHA256.txt',
    'licenses\OpenXR-SDK\LICENSE.txt',
    'licenses\JsonCpp\LICENSE.txt',
    'licenses\nuget\Microsoft.WindowsAppSDK.ML_1.8.2141\license.txt',
    'licenses\nuget\Microsoft.WindowsAppSDK.ML_1.8.2141\ThirdPartyNotices.txt'
)
foreach ($requiredFile in $requiredTopLevelFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $resolvedPayload $requiredFile) -PathType Leaf)) {
        throw "Required installer payload file is missing: $requiredFile"
    }
}

$allItems = @(Get-ChildItem -LiteralPath $resolvedPayload -Recurse -Force)
if ($allItems.Count -gt 2048) {
    throw "Installer payload contains too many entries: $($allItems.Count)"
}

$prohibitedNames = @(
    'CoDWaW.exe', 'CoDWaWmp.exe', 't4sp.exe', 't4mp.exe', 'binkw32.dll',
    'PeZBOTWAW_005p.zip', 'mod.ff', 'PeZBOTWaW.iwd',
    'PROVENANCE_AUDIT.md', 'PROVENANCE-AUDIT.md', 'components.json',
    'WorldAtWarVR.exe', 'WorldAtWarVR.dll', 'WorldAtWarVR.Launcher.dll',
    'WorldAtWarVR.Launcher.Core.dll', 'WorldAtWarVR.ico', 'WorldAtWarVR.pri'
)
$prohibitedExtensions = @(
    '.bik', '.d3dbsp', '.dmp', '.exp', '.ff', '.iwd', '.lib', '.log',
    '.obj', '.pdb', '.zip', '.sha256'
)

$payloadFiles = @()
$relativePaths = [System.Collections.Generic.List[string]]::new()
$pathSet = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($item in $allItems) {
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Installer payload must not contain links or junctions: $($item.FullName)"
    }
    if ($item.PSIsContainer) {
        continue
    }

    $relativePath = [System.IO.Path]::GetRelativePath(
        $resolvedPayload,
        $item.FullName).Replace('/', '\')
    if ([System.IO.Path]::IsPathRooted($relativePath) -or
        $relativePath.StartsWith('\') -or
        $relativePath.Contains(':') -or
        $relativePath -match '(^|\\)\.\.?($|\\)') {
        throw "Unsafe relative path entered the installer payload: $relativePath"
    }
    if (-not $pathSet.Add($relativePath)) {
        throw "Case-insensitive duplicate path entered the installer payload: $relativePath"
    }
    if ($prohibitedNames -icontains $item.Name -or
        $item.Name -ilike 'plutonium*' -or
        $item.Name -ieq 'payload-manifest.txt' -or
        $item.Name -ilike '*-Setup.exe' -or
        $prohibitedExtensions -icontains $item.Extension) {
        throw "Prohibited game, third-party mod, build-only, or nested release file entered the installer payload: $relativePath"
    }

    $payloadFiles += $item
    $relativePaths.Add($relativePath)
}

if ($payloadFiles.Count -lt $requiredTopLevelFiles.Count) {
    throw 'Installer payload inventory is incomplete.'
}
$payloadBytes = ($payloadFiles | Measure-Object -Property Length -Sum).Sum
if ($payloadBytes -gt 1GB) {
    throw "Installer payload exceeds the 1 GiB safety limit: $payloadBytes bytes"
}

$payloadChecksumManifest = Join-Path $resolvedPayload 'PAYLOAD-SHA256.txt'
$checksumLines = [System.IO.File]::ReadAllLines($payloadChecksumManifest)
$checksumMarkerIndex = [Array]::IndexOf($checksumLines, 'SHA-256:')
if ($checksumMarkerIndex -lt 0) {
    throw 'PAYLOAD-SHA256.txt does not contain its required SHA-256 section.'
}
$declaredChecksumPaths = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
for ($lineIndex = $checksumMarkerIndex + 1;
     $lineIndex -lt $checksumLines.Count;
     ++$lineIndex) {
    $line = $checksumLines[$lineIndex]
    if ([string]::IsNullOrWhiteSpace($line)) {
        continue
    }
    $match = [regex]::Match($line, '^([A-Fa-f0-9]{64})  (.+)$')
    if (-not $match.Success) {
        throw "PAYLOAD-SHA256.txt contains a malformed entry: $line"
    }
    $declaredPath = $match.Groups[2].Value.Replace('/', '\')
    if ([System.IO.Path]::IsPathRooted($declaredPath) -or
        $declaredPath.StartsWith('\') -or
        $declaredPath.Contains(':') -or
        $declaredPath -match '(^|\\)\.\.?($|\\)' -or
        $declaredPath -ieq 'PAYLOAD-SHA256.txt') {
        throw "PAYLOAD-SHA256.txt contains an unsafe or self-referential path: $declaredPath"
    }
    if (-not $declaredChecksumPaths.Add($declaredPath)) {
        throw "PAYLOAD-SHA256.txt contains a duplicate path: $declaredPath"
    }
    $declaredFile = Join-Path $resolvedPayload $declaredPath
    if (-not (Test-Path -LiteralPath $declaredFile -PathType Leaf)) {
        throw "PAYLOAD-SHA256.txt names a missing file: $declaredPath"
    }
    $actualHash = (Get-FileHash -LiteralPath $declaredFile -Algorithm SHA256).Hash
    if (-not $actualHash.Equals(
            $match.Groups[1].Value,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "PAYLOAD-SHA256.txt has the wrong hash for: $declaredPath"
    }
}

$expectedChecksummedPaths = @(
    $relativePaths |
        Where-Object { $_ -ine 'PAYLOAD-SHA256.txt' })
if ($declaredChecksumPaths.Count -ne $expectedChecksummedPaths.Count) {
    throw 'PAYLOAD-SHA256.txt does not cover the complete payload inventory.'
}
foreach ($expectedPath in $expectedChecksummedPaths) {
    if (-not $declaredChecksumPaths.Contains($expectedPath)) {
        throw "PAYLOAD-SHA256.txt omits a payload file: $expectedPath"
    }
}

$inventory = $relativePaths.ToArray()
[Array]::Sort($inventory, [StringComparer]::OrdinalIgnoreCase)
$manifestText = ($inventory -join "`r`n") + "`r`n"
$manifestBytes = [System.Text.UTF8Encoding]::new($false).GetBytes($manifestText)
$sha256Provider = [System.Security.Cryptography.SHA256]::Create()
try {
    $manifestSha256 = [Convert]::ToHexString(
        $sha256Provider.ComputeHash($manifestBytes))
}
finally {
    $sha256Provider.Dispose()
}

Write-Host "Validated installer payload: $($inventory.Count) files, $payloadBytes bytes"
Write-Host "Payload manifest SHA-256: $manifestSha256"
if ($ValidateOnly) {
    if ($PrintInventory) {
        foreach ($relativePath in $inventory) {
            Write-Host "  $relativePath"
        }
    }
    Write-Host 'Validation-only mode completed; no installer was created.'
    return
}

if (Test-Path -LiteralPath $resolvedOutput) {
    Assert-NormalDirectory -Path $resolvedOutput -Description 'Installer output directory'
}
else {
    New-Item -ItemType Directory -Path $resolvedOutput | Out-Null
}
if ((Test-Path -LiteralPath $installerPath -PathType Leaf) -or
    (Test-Path -LiteralPath $sidecarPath -PathType Leaf)) {
    throw "Installer output already exists; use a fresh output directory: $installerPath"
}

$compilerPath = Resolve-InnoCompiler
$compilerSha256 = (Get-FileHash -LiteralPath $compilerPath -Algorithm SHA256).Hash
if (-not $compilerSha256.Equals(
        $ExpectedInnoCompilerSha256,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Inno Setup compiler hash does not match the pinned 6.7.3 compiler: $compilerSha256"
}

$temporaryDirectory = Join-Path `
    ([System.IO.Path]::GetTempPath()) `
    ("worldwarvr-installer-manifest-{0}" -f [Guid]::NewGuid().ToString('N'))
$manifestPath = Join-Path $temporaryDirectory 'payload-manifest.txt'
try {
    New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
    [System.IO.File]::WriteAllBytes($manifestPath, $manifestBytes)

    & $compilerPath `
        '/Qp' `
        "/DPayloadDir=$resolvedPayload" `
        "/DPayloadManifest=$manifestPath" `
        "/DPayloadManifestSha256=$manifestSha256" `
        "/DPayloadCount=$($inventory.Count)" `
        "/DInstallerOutputDir=$resolvedOutput" `
        "/DProductVersion=$Version" `
        "/DProductFileVersion=$fileVersion" `
        "/DProductAppId={{$($AppId.ToUpperInvariant())}" `
        $setupScript
    if ($LASTEXITCODE -ne 0) {
        throw "Inno Setup failed with exit code $LASTEXITCODE"
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryDirectory -PathType Container) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}

if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
    throw "Inno Setup completed without producing the expected file: $installerPath"
}
$header = [byte[]]::new(2)
$installerStream = [System.IO.File]::OpenRead($installerPath)
try {
    $headerBytesRead = $installerStream.Read($header, 0, $header.Length)
}
finally {
    $installerStream.Dispose()
}
if ($headerBytesRead -ne 2 -or $header[0] -ne 0x4D -or $header[1] -ne 0x5A) {
    throw 'The generated installer does not have a valid Windows executable header.'
}

$signature = Get-AuthenticodeSignature -LiteralPath $installerPath
if ($RequireSignature -and $signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
    throw "The installer is not validly Authenticode-signed: $($signature.Status)"
}

$installer = Get-Item -LiteralPath $installerPath
if ($installer.Length -lt 1MB) {
    throw "The generated installer is unexpectedly small: $($installer.Length) bytes"
}
$installerSha256 = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash
$sidecarText = "$installerSha256  $installerName`n"
[System.IO.File]::WriteAllText(
    $sidecarPath,
    $sidecarText,
    [System.Text.UTF8Encoding]::new($false))

Write-Host "World War VR installer ready: $installerPath"
Write-Host "Size: $($installer.Length) bytes"
Write-Host "SHA-256: $installerSha256"
Write-Host "Authenticode: $($signature.Status)"
Write-Host "Checksum: $sidecarPath"
