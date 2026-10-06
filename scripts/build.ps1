[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [switch]$Clean,
    [switch]$SkipTests,
    [string]$Target = '',

    [string]$BuildRoot = '',
    [string]$TempRoot = ''
)

$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath(
    (Split-Path -Parent $PSScriptRoot))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found.'
}

$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) {
    throw 'Visual Studio 2022 C++ x86 build tools were not found.'
}

$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvarsall.bat'
$cmakeBin = Join-Path $vsRoot `
    'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$cmake = Join-Path $cmakeBin 'cmake.exe'
$ctest = Join-Path $cmakeBin 'ctest.exe'
if (-not (Test-Path -LiteralPath $cmake -PathType Leaf) -or
    -not (Test-Path -LiteralPath $ctest -PathType Leaf)) {
    throw "Visual Studio's bundled CMake/CTest tools were not found under: $cmakeBin"
}

# Do not resolve CMake through PATH after vcvarsall. This machine also has a
# separate CMake 4.x installation whose Visual Studio generator selects
# materially different compiler/linker defaults (including PE hardening
# properties). Pin the audited bundled generator so clean builds cannot vary
# with the invoking shell's PATH order.
$requiredCMakeVersion = 'cmake version 3.31.6-msvc6'
$actualCMakeVersion = (& $cmake --version | Select-Object -First 1).Trim()
if ($actualCMakeVersion -ne $requiredCMakeVersion) {
    throw "Unsupported deterministic CMake toolchain: '$actualCMakeVersion' (required '$requiredCMakeVersion')."
}

$buildName = $Configuration.ToLowerInvariant()
$resolvedBuildRoot = if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build'))
}
else {
    [System.IO.Path]::GetFullPath($BuildRoot)
}
if (-not (Test-Path -LiteralPath $resolvedBuildRoot)) {
    New-Item -ItemType Directory -Path $resolvedBuildRoot | Out-Null
}
$buildRootItem = Get-Item -LiteralPath $resolvedBuildRoot -Force
if (-not $buildRootItem.PSIsContainer -or
    ($buildRootItem.Attributes -band
        [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Build root must be a normal directory: $resolvedBuildRoot"
}
$buildDir = [System.IO.Path]::GetFullPath(
    (Join-Path $resolvedBuildRoot "vs-$buildName"))

if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    $resolvedBuild = (Resolve-Path -LiteralPath $buildDir).Path
    $resolvedParent = [System.IO.Path]::GetDirectoryName($resolvedBuild)
    if (-not $resolvedParent.Equals(
            $resolvedBuildRoot.TrimEnd('\'),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean unexpected path: $resolvedBuild"
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

$resolvedTempRoot = ''
if (-not [string]::IsNullOrWhiteSpace($TempRoot)) {
    $resolvedTempRoot = [System.IO.Path]::GetFullPath($TempRoot)
    if (-not (Test-Path -LiteralPath $resolvedTempRoot)) {
        New-Item -ItemType Directory -Path $resolvedTempRoot | Out-Null
    }
    $tempRootItem = Get-Item -LiteralPath $resolvedTempRoot -Force
    if (-not $tempRootItem.PSIsContainer -or
        ($tempRootItem.Attributes -band
            [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Temporary root must be a normal directory: $resolvedTempRoot"
    }
}

$tests = if ($SkipTests) { 'OFF' } else { 'ON' }
$configure = "`"$cmake`" -S `"$repoRoot`" -B `"$buildDir`" -G `"Visual Studio 17 2022`" -A Win32 -DWAWVR_BUILD_TESTS=$tests -DWAWVR_BUILD_XR=ON"
$targetArgument = if ($Target) { " --target `"$Target`"" } else { '' }
$build = "`"$cmake`" --build `"$buildDir`" --config $Configuration$targetArgument -- /m:1 /nr:false"
$test = "`"$ctest`" --test-dir `"$buildDir`" -C $Configuration --output-on-failure"

$command = "`"$vcvars`" x86 >nul && $configure && $build"
if (-not $SkipTests -and -not $Target) {
    $command += " && $test"
}

$priorTemp = $env:TEMP
$priorTmp = $env:TMP
try {
    if ($resolvedTempRoot) {
        $env:TEMP = $resolvedTempRoot
        $env:TMP = $resolvedTempRoot
    }
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "World War VR build failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:TEMP = $priorTemp
    $env:TMP = $priorTmp
}

Write-Host "World War VR $Configuration build completed: $buildDir"
Write-Host "Deterministic CMake toolchain: $actualCMakeVersion"
