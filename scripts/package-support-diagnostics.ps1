[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PortableZip,

    [Parameter(Mandatory = $true)]
    [string]$MatchingSourceZip,

    [Parameter(Mandatory = $true)]
    [string]$NativeBuildDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [string]$BuildLabel = 'beta3-support-2026-09-19',

    [ValidateSet('Primary', 'Secondary', 'All')]
    [string]$PackageSet = 'Primary'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$portableZip = [System.IO.Path]::GetFullPath($PortableZip)
$matchingSourceZip = [System.IO.Path]::GetFullPath($MatchingSourceZip)
$nativeBuildDirectory = [System.IO.Path]::GetFullPath($NativeBuildDirectory)
$outputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$supportSource = Join-Path $repoRoot 'support\diagnostics'
$cli = Join-Path $nativeBuildDirectory 'launcher\Release\wawvr-launcher-cli.exe'

foreach ($input in @($portableZip, $matchingSourceZip, $cli)) {
    if (-not (Test-Path -LiteralPath $input -PathType Leaf)) {
        throw "Required support-package input is missing: $input"
    }
}
if (-not (Test-Path -LiteralPath $supportSource -PathType Container)) {
    throw "Support source is missing: $supportSource"
}
if (Test-Path -LiteralPath $outputDirectory) {
    $outputItem = Get-Item -LiteralPath $outputDirectory -Force
    if (-not $outputItem.PSIsContainer -or
        ($outputItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Output must be a normal directory: $outputDirectory"
    }
}
else {
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
}

$stageParent = Join-Path $outputDirectory ".support-staging-$PID"
if (Test-Path -LiteralPath $stageParent) {
    throw "Refusing to reuse support staging: $stageParent"
}
New-Item -ItemType Directory -Path $stageParent | Out-Null

$packages = @(
    [pscustomobject]@{
        Slug = 'Index-SteamVR'
        Issue = 'IndexSteamVR'
        Readme = 'README-Index-SteamVR.txt'
        Launch = $true
        Set = 'Primary'
    },
    [pscustomobject]@{
        Slug = 'Pimax'
        Issue = 'Pimax'
        Readme = 'README-Pimax.txt'
        Launch = $true
        Set = 'Primary'
    },
    [pscustomobject]@{
        Slug = 'Flat-Launch'
        Issue = 'FlatLaunch'
        Readme = 'README-Flat-Launch.txt'
        Launch = $true
        Set = 'Primary'
    },
    [pscustomobject]@{
        Slug = 'German-Uncut-Inventory'
        Issue = 'GermanUncut'
        Readme = 'README-German-Uncut.txt'
        Launch = $false
        Set = 'Primary'
    },
    [pscustomobject]@{
        Slug = 'Custom-Zombies'
        Issue = 'CustomZombies'
        Readme = 'README-Custom-Zombies.txt'
        Launch = $true
        Set = 'Secondary'
    },
    [pscustomobject]@{
        Slug = 'Random-Melee'
        Issue = 'RandomMelee'
        Readme = 'README-Random-Melee.txt'
        Launch = $true
        Set = 'Secondary'
    },
    [pscustomobject]@{
        Slug = 'YangYoung-Regression'
        Issue = 'YangYoungRegression'
        Readme = 'README-YangYoung-Regression.txt'
        Launch = $true
        Set = 'Secondary'
    },
    [pscustomobject]@{
        Slug = 'Pause-Menu'
        Issue = 'PauseMenu'
        Readme = 'README-Pause-Menu.txt'
        Launch = $true
        Set = 'Secondary'
    }
)

if ($PackageSet -ne 'All') {
    $packages = @($packages | Where-Object { $_.Set -eq $PackageSet })
}

$generated = [System.Collections.Generic.List[string]]::new()
try {
    foreach ($package in $packages) {
        $stage = Join-Path $stageParent "WorldWarVR-$($package.Slug)-Diagnostic"
        New-Item -ItemType Directory -Path $stage | Out-Null
        Expand-Archive -LiteralPath $portableZip -DestinationPath $stage

        $payloadChildren = @(Get-ChildItem -LiteralPath $stage -Force)
        if ($payloadChildren.Count -eq 1 -and $payloadChildren[0].PSIsContainer) {
            $nested = $payloadChildren[0].FullName
            foreach ($child in @(Get-ChildItem -LiteralPath $nested -Force)) {
                Move-Item -LiteralPath $child.FullName -Destination $stage
            }
            Remove-Item -LiteralPath $nested
        }

        Copy-Item -LiteralPath $cli -Destination (
            Join-Path $stage 'wawvr-launcher-cli.exe')
        $supportDestination = Join-Path $stage 'support'
        New-Item -ItemType Directory -Path $supportDestination | Out-Null
        Copy-Item -LiteralPath (
            Join-Path $supportSource 'Start-WorldWarVR-Diagnostic.ps1') -Destination $supportDestination
        Copy-Item -LiteralPath (
            Join-Path $supportSource 'Collect-WorldWarVR-Diagnostics.ps1') -Destination $supportDestination
        Copy-Item -LiteralPath (
            Join-Path $supportSource $package.Readme) -Destination (
            Join-Path $stage 'SUPPORT-TEST.txt')
        [System.IO.File]::WriteAllLines(
            (Join-Path $stage 'TEST-NOTES.txt'),
            @(
                'Please add any observations here before running COLLECT-DIAGNOSTICS.cmd.',
                'Approximate time of the issue:',
                'Mission/map and weapon:',
                'Exact action immediately before the issue:',
                'What appeared in the headset:',
                'What appeared on the desktop:',
                'Did the issue happen every time:'
            ),
            [System.Text.UTF8Encoding]::new($false))

        $startLines = @(
            '@echo off',
            'setlocal',
            'cd /d "%~dp0"',
            ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0support\Start-WorldWarVR-Diagnostic.ps1" -Issue {0}' -f $package.Issue),
            'if errorlevel 1 pause'
        )
        if ($package.Launch) {
            [System.IO.File]::WriteAllLines(
                (Join-Path $stage 'START-DIAGNOSTIC.cmd'),
                $startLines,
                [System.Text.Encoding]::ASCII)
        }
        $collectLines = @(
            '@echo off',
            'setlocal',
            'cd /d "%~dp0"',
            ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0support\Collect-WorldWarVR-Diagnostics.ps1" -Issue {0}' -f $package.Issue),
            'if errorlevel 1 pause'
        )
        [System.IO.File]::WriteAllLines(
            (Join-Path $stage 'COLLECT-DIAGNOSTICS.cmd'),
            $collectLines,
            [System.Text.Encoding]::ASCII)

        $sourceNotice = @(
            'World War VR diagnostic build - matching source notice',
            '',
            "Build label: $BuildLabel",
            'World War VR is GPLv3. The complete matching source is supplied as a separate ZIP beside this diagnostic package.',
            "Matching source filename: $([System.IO.Path]::GetFileName($matchingSourceZip))",
            'No Call of Duty game files are included.'
        )
        [System.IO.File]::WriteAllLines(
            (Join-Path $stage 'MATCHING-SOURCE-NOTICE.txt'),
            $sourceNotice,
            [System.Text.UTF8Encoding]::new($false))
        [System.IO.File]::WriteAllText(
            (Join-Path $stage 'SUPPORT-PACKAGE-ID.txt'),
            "$BuildLabel/$($package.Issue)`r`n",
            [System.Text.UTF8Encoding]::new($false))

        $destination = Join-Path $outputDirectory (
            "WorldWarVR-$BuildLabel-$($package.Slug).zip")
        if (Test-Path -LiteralPath $destination) {
            throw "Refusing to overwrite existing support package: $destination"
        }
        Compress-Archive -LiteralPath $stage -DestinationPath $destination -CompressionLevel Optimal
        $generated.Add($destination)
    }

    $sourceDestination = Join-Path $outputDirectory (
        "WorldWarVR-$BuildLabel-Matching-Source.zip")
    if (Test-Path -LiteralPath $sourceDestination) {
        throw "Refusing to overwrite existing matching source: $sourceDestination"
    }
    Copy-Item -LiteralPath $matchingSourceZip -Destination $sourceDestination
    $generated.Add($sourceDestination)

    $checksumLines = foreach ($path in $generated) {
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        "$hash  $([System.IO.Path]::GetFileName($path))"
    }
    [System.IO.File]::WriteAllLines(
        (Join-Path $outputDirectory 'SHA256SUMS.txt'),
        $checksumLines,
        [System.Text.UTF8Encoding]::new($false))
}
finally {
    if (Test-Path -LiteralPath $stageParent -PathType Container) {
        $resolvedStage = [System.IO.Path]::GetFullPath($stageParent)
        $resolvedOutput = $outputDirectory.TrimEnd('\') + '\'
        if (-not $resolvedStage.StartsWith(
                $resolvedOutput,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove staging outside output root: $resolvedStage"
        }
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}

Write-Host "Support packages ready: $outputDirectory"
foreach ($path in $generated) {
    Write-Host "  $path"
}
