[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
    [string]$PackageName = 'WorldWarVR',

    [string]$BuildRoot = '',
    [string]$TempRoot = '',
    [string]$OutputRoot = '',
    [string]$StagingRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$buildScript = Join-Path $repoRoot 'scripts\build.ps1'
$resolvedBuildRoot = if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
}
else {
    [System.IO.Path]::GetFullPath($BuildRoot)
}
$buildDir = Join-Path $resolvedBuildRoot 'vs-release'
$distRoot = if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'dist'))
}
else {
    [System.IO.Path]::GetFullPath($OutputRoot)
}
$packageDir = [System.IO.Path]::GetFullPath((Join-Path $distRoot $PackageName))
$resolvedStagingRoot = if ([string]::IsNullOrWhiteSpace($StagingRoot)) {
    $distRoot
}
else {
    [System.IO.Path]::GetFullPath($StagingRoot)
}
$stagingDir = [System.IO.Path]::GetFullPath(
    (Join-Path $resolvedStagingRoot ".$PackageName.staging-$PID"))
$fixedTimestamp = [DateTime]::SpecifyKind(
    [DateTime]::Parse('2000-01-01T00:00:00'),
    [DateTimeKind]::Utc)

function Assert-DirectChildPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Candidate,

        [Parameter(Mandatory = $true)]
        [string]$Parent
    )

    $fullCandidate = [System.IO.Path]::GetFullPath($Candidate)
    $fullParent = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\')
    $candidateParent = [System.IO.Path]::GetDirectoryName($fullCandidate).TrimEnd('\')
    if (-not $candidateParent.Equals(
            $fullParent,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing package operation outside the fixed dist root: $fullCandidate"
    }
    return $fullCandidate
}

function Assert-NormalDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    $item = Get-Item -LiteralPath $Path -Force
    if (-not $item.PSIsContainer) {
        throw "$Description is not a directory: $Path"
    }
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description must not be a junction or symbolic link: $Path"
    }
}

function Resolve-PackageDestination {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [System.IO.Path]::IsPathRooted($RelativePath)) {
        throw "Package destination must be a non-rooted relative path: $RelativePath"
    }

    $normalized = $RelativePath.Replace('/', '\')
    $segments = @($normalized.Split('\'))
    if ($segments.Count -eq 0 -or
        $segments.Where({ [string]::IsNullOrWhiteSpace($_) -or $_ -in @('.', '..') }).Count -ne 0) {
        throw "Package destination contains an unsafe path segment: $RelativePath"
    }

    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $fullDestination = [System.IO.Path]::GetFullPath(
        (Join-Path $fullRoot $normalized))
    $requiredPrefix = $fullRoot + '\'
    if (-not $fullDestination.StartsWith(
            $requiredPrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Package destination escapes the staging root: $RelativePath"
    }
    return $fullDestination
}

function Get-PackageRelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,

        [Parameter(Mandatory = $true)]
        [string]$FullPath
    )

    $relativePath = [System.IO.Path]::GetRelativePath(
        [System.IO.Path]::GetFullPath($Root),
        [System.IO.Path]::GetFullPath($FullPath))
    $null = Resolve-PackageDestination -Root $Root -RelativePath $relativePath
    return $relativePath.Replace('\', '/')
}

function Remove-ValidatedPackageDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Parent
    )

    $validated = Assert-DirectChildPath -Candidate $Path -Parent $Parent
    if (-not (Test-Path -LiteralPath $validated)) {
        return
    }
    Assert-NormalDirectory -Path $validated -Description 'Package directory'
    Remove-Item -LiteralPath $validated -Recurse -Force
}

if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
    throw "Build script is missing: $buildScript"
}

# A package is never assembled from stale or untested binaries.
$buildArguments = @{
    Configuration = 'Release'
    BuildRoot = $resolvedBuildRoot
}
if (-not [string]::IsNullOrWhiteSpace($TempRoot)) {
    $buildArguments.TempRoot = [System.IO.Path]::GetFullPath($TempRoot)
}
& $buildScript @buildArguments

$files = @(
    [pscustomobject]@{
        Source = Join-Path $buildDir 'launcher\Release\WorldWarVR.exe'
        RelativePath = 'WorldWarVR.exe'
    },
    [pscustomobject]@{
        Source = Join-Path $buildDir 'launcher\Release\WorldWarVR-Multiplayer.exe'
        RelativePath = 'WorldWarVR-Multiplayer.exe'
    },
    [pscustomobject]@{
        Source = Join-Path $buildDir 'launcher\Release\wawvr-launcher.exe'
        RelativePath = 'wawvr-launcher.exe'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'launcher\scripts\import_pezbot.ps1'
        RelativePath = 'WaWVR-PeZBOT-Import.ps1'
    },
    [pscustomobject]@{
        Source = Join-Path $buildDir 'src\mod\Release\WorldWarVR.dll'
        RelativePath = 'WorldWarVR.dll'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'README.md'
        RelativePath = 'README.md'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'docs\CONTROLS.md'
        RelativePath = 'docs\CONTROLS.md'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'docs\OFFLINE_MULTIPLAYER.md'
        RelativePath = 'docs\OFFLINE_MULTIPLAYER.md'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'docs\architecture.md'
        RelativePath = 'docs\architecture.md'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'docs\test-plan.md'
        RelativePath = 'docs\test-plan.md'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'LICENSE'
        RelativePath = 'LICENSE'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'third_party\openxr-sdk\LICENSE'
        RelativePath = 'LICENSE-OPENXR-SDK.txt'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'third_party\openxr-sdk\src\external\jsoncpp\LICENSE'
        RelativePath = 'LICENSE-JSONCPP.txt'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'THIRD-PARTY-NOTICES.md'
        RelativePath = 'THIRD-PARTY-NOTICES.md'
    }
)

# The stock SP menu's hard-coded name is created only inside the user's
# isolated runtime by copying this package's WorldWarVR.exe. Never emit a
# package artifact under the proprietary MP executable name.
$userFacingLaunchers = @(
    $files | Where-Object { $_.RelativePath -ieq 'WorldWarVR.exe' })
if ($userFacingLaunchers.Count -ne 1) {
    throw 'Release package must contain exactly one WorldWarVR.exe launcher source artifact.'
}
$multiplayerLaunchers = @(
    $files | Where-Object {
        $_.RelativePath -ieq 'WorldWarVR-Multiplayer.exe'
    })
if ($multiplayerLaunchers.Count -ne 1) {
    throw 'Release package must contain exactly one dedicated multiplayer launcher.'
}
if (@($files | Where-Object { $_.RelativePath -ieq 'CoDWaWmp.exe' }).Count -ne 0) {
    throw 'CoDWaWmp.exe is a runtime-only launcher alias and must never enter the package.'
}

$destinationPaths = [System.Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file.Source -PathType Leaf)) {
        throw "Required Release package input is missing: $($file.Source)"
    }
    $sourceItem = Get-Item -LiteralPath $file.Source -Force
    if (($sourceItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Release package input must not be a symbolic link: $($file.Source)"
    }
    $normalizedDestination = $file.RelativePath.Replace('\', '/')
    if (-not $destinationPaths.Add($normalizedDestination)) {
        throw "Duplicate Release package destination: $normalizedDestination"
    }
}

if (Test-Path -LiteralPath $distRoot) {
    Assert-NormalDirectory -Path $distRoot -Description 'Distribution root'
}
else {
    New-Item -ItemType Directory -Path $distRoot | Out-Null
}
if (Test-Path -LiteralPath $resolvedStagingRoot) {
    Assert-NormalDirectory `
        -Path $resolvedStagingRoot `
        -Description 'Package staging root'
}
else {
    New-Item -ItemType Directory -Path $resolvedStagingRoot | Out-Null
}

$null = Assert-DirectChildPath -Candidate $packageDir -Parent $distRoot
$null = Assert-DirectChildPath `
    -Candidate $stagingDir `
    -Parent $resolvedStagingRoot
if (Test-Path -LiteralPath $stagingDir) {
    throw "Refusing to reuse an existing package staging directory: $stagingDir"
}

$stagingCreated = $false
try {
    New-Item -ItemType Directory -Path $stagingDir | Out-Null
    $stagingCreated = $true

    foreach ($file in $files) {
        $destination = Resolve-PackageDestination `
            -Root $stagingDir `
            -RelativePath $file.RelativePath
        $destinationParent = [System.IO.Path]::GetDirectoryName($destination)
        if (-not (Test-Path -LiteralPath $destinationParent)) {
            New-Item -ItemType Directory -Path $destinationParent | Out-Null
        }
        Assert-NormalDirectory `
            -Path $destinationParent `
            -Description 'Package destination parent'
        Copy-Item -LiteralPath $file.Source -Destination $destination
    }

    $payloadFiles = @(Get-ChildItem -LiteralPath $stagingDir -File -Recurse -Force)
    if ($payloadFiles.Count -ne $files.Count) {
        throw "Package payload validation failed: expected $($files.Count), found $($payloadFiles.Count)"
    }

    $payloadInventory = @()
    foreach ($payloadFile in $payloadFiles) {
        $relativePath = Get-PackageRelativePath `
            -Root $stagingDir `
            -FullPath $payloadFile.FullName
        if (-not $destinationPaths.Contains($relativePath)) {
            throw "Unexpected file entered package staging: $relativePath"
        }
        $payloadInventory += [pscustomobject]@{
            Item = $payloadFile
            RelativePath = $relativePath
        }
    }

    $manifestLines = @(
        'World War VR standalone Release package'
        'Package layout: 5'
        'World War VR release version: 0.4.0-alpha.2'
        'World War VR binary version: 0.4.0-t4-vr-mp'
        'Default packed source resolution: 2560x1440 (1280x1440 per eye)'
        'Performance packed source resolution: 1600x900'
        'Recovery packed source resolution: 1024x768'
        'User-facing default: configured WorldWarVR.exe opens the stock Zombies frontend/menu'
        'One-click offline multiplayer: WorldWarVR-Multiplayer.exe'
        'Direct Nacht: WorldWarVR.exe --launch'
        'Direct Der Riese: WorldWarVR.exe --launch --der-riese'
        'Offline multiplayer: WorldWarVR.exe --launch --multiplayer'
        'Optional PeZBOT: user supplies exact PeZBOTWAW_005p.zip; it is never packaged.'
        'Stock-menu MP handoff: WorldWarVR.exe is copied only at runtime as CoDWaWmp.exe.'
        'The runtime-only CoDWaWmp.exe alias is this launcher, never a packaged game executable.'
        'No Call of Duty executable, fastfile, IWD, or other game asset is included.'
        ''
        'SHA-256:'
    )
    foreach ($entry in ($payloadInventory | Sort-Object RelativePath)) {
        $hash = (Get-FileHash -LiteralPath $entry.Item.FullName -Algorithm SHA256).Hash
        $manifestLines += "$hash  $($entry.RelativePath)"
    }
    $manifestPath = Join-Path $stagingDir 'PACKAGE-SHA256.txt'
    $manifestText = ($manifestLines -join "`n") + "`n"
    [System.IO.File]::WriteAllText(
        $manifestPath,
        $manifestText,
        [System.Text.UTF8Encoding]::new($false))

    $packagedFiles = @(
        Get-ChildItem -LiteralPath $stagingDir -File -Recurse -Force)
    $expectedCount = $files.Count + 1
    if ($packagedFiles.Count -ne $expectedCount) {
        throw "Package file-count validation failed: expected $expectedCount, found $($packagedFiles.Count)"
    }
    $prohibitedNames = @(
        'CoDWaW.exe',
        'CoDWaWmp.exe',
        't4sp.exe',
        't4mp.exe',
        'binkw32.dll',
        'plutonium.exe',
        'plutonium-bootstrapper-win32.exe',
        'PeZBOTWAW_005p.zip'
    )
    $prohibitedExtensions = @('.ff', '.iwd', '.d3dbsp', '.bik')
    foreach ($packagedFile in $packagedFiles) {
        $relativePath = Get-PackageRelativePath `
            -Root $stagingDir `
            -FullPath $packagedFile.FullName
        if ($prohibitedNames -icontains $packagedFile.Name -or
            $packagedFile.Name -ilike 'plutonium*' -or
            $prohibitedExtensions -icontains $packagedFile.Extension) {
            throw "Proprietary game content entered package staging: $relativePath"
        }
        if (($packagedFile.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Package file must not be a symbolic link: $relativePath"
        }
        $packagedFile.LastWriteTimeUtc = $fixedTimestamp
    }
    foreach ($packagedDirectory in @(
            Get-ChildItem -LiteralPath $stagingDir -Directory -Recurse -Force |
                Sort-Object FullName -Descending)) {
        Assert-NormalDirectory `
            -Path $packagedDirectory.FullName `
            -Description 'Package subdirectory'
        $packagedDirectory.LastWriteTimeUtc = $fixedTimestamp
    }

    # Replace only the exact, fixed package directory after the new package is
    # complete. A failed build or staging pass leaves the prior package intact.
    Remove-ValidatedPackageDirectory -Path $packageDir -Parent $distRoot
    Move-Item -LiteralPath $stagingDir -Destination $packageDir
    $stagingCreated = $false
    (Get-Item -LiteralPath $packageDir -Force).LastWriteTimeUtc = $fixedTimestamp
}
catch {
    if ($stagingCreated -and (Test-Path -LiteralPath $stagingDir)) {
        Remove-ValidatedPackageDirectory `
            -Path $stagingDir `
            -Parent $resolvedStagingRoot
    }
    throw
}

Write-Host "World War VR Release package ready: $packageDir"
Write-Host 'Launch configured WorldWarVR.exe for the Zombies frontend/menu (--game-dir or WAWVR_GAME_DIR).'
Write-Host 'Launch WorldWarVR-Multiplayer.exe for one-click standalone offline multiplayer.'
Write-Host 'Use WorldWarVR.exe --launch for direct Nacht.'
Write-Host 'Use WorldWarVR.exe --launch --der-riese for direct Der Riese.'
Write-Host 'Use WorldWarVR.exe --launch --multiplayer for standalone offline multiplayer.'
Write-Host 'For bots, supply the exact user-owned PeZBOTWAW_005p.zip; see docs\OFFLINE_MULTIPLAYER.md.'
