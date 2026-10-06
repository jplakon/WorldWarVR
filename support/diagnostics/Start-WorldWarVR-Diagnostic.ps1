[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        'IndexSteamVR',
        'Pimax',
        'FlatLaunch',
        'CustomZombies',
        'RandomMelee',
        'YangYoungRegression',
        'PauseMenu')]
    [string]$Issue
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$packageRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$launcher = Join-Path $packageRoot 'WorldWarVR.exe'
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    throw "WorldWarVR.exe is missing from $packageRoot"
}

$env:WAWVR_FRAME_TIMING_DIAGNOSTICS = '1'
$env:WAWVR_ORIENTATION_DIAGNOSTICS = '1'
$orientationDirectory = Join-Path $packageRoot 'support-orientation-captures'
New-Item -ItemType Directory -Path $orientationDirectory -Force | Out-Null
$env:WAWVR_ORIENTATION_DIAG_DIR = $orientationDirectory
$env:WAWVR_SUPPORT_DIAGNOSTIC_KIND = $Issue

switch ($Issue) {
    'YangYoungRegression' {
        $env:WAWVR_MOSIN_BULLET_DIAGNOSTICS = '1'
        $env:WAWVR_POST_T4_VISIBLE_AIM_DIAGNOSTICS = '1'
        $env:WAWVR_WEAPON_PIPELINE_TRACE = '1'
        $env:WAWVR_FX_STEREO_DIAGNOSTICS = '1'
    }
    'CustomZombies' {
        # Keep this run close to the shipping path. Frame/orientation logs plus
        # the collector's mod inventory and crash-event report are sufficient.
    }
}

$logPath = Join-Path $packageRoot 'WorldWarVR.log'
if (Test-Path -LiteralPath $logPath -PathType Leaf) {
    $archiveDirectory = Join-Path $packageRoot 'support-previous-logs'
    New-Item -ItemType Directory -Path $archiveDirectory -Force | Out-Null
    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    Move-Item -LiteralPath $logPath -Destination (
        Join-Path $archiveDirectory "WorldWarVR-before-$Issue-$timestamp.log")
}

Write-Host ''
Write-Host "Starting World War VR support test: $Issue"
Write-Host 'Use the launcher settings described in SUPPORT-TEST.txt.'
Write-Host 'After testing, close the game and launcher, then run COLLECT-DIAGNOSTICS.cmd.'
Write-Host ''

$process = Start-Process -FilePath $launcher -WorkingDirectory $packageRoot -PassThru
$process.WaitForExit()

Write-Host ''
Write-Host 'The launcher has closed. Close World at War if it is still running, then run COLLECT-DIAGNOSTICS.cmd.'
Read-Host 'Press Enter to close this window'
