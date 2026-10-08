[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidatePattern('^(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-(?:alpha|beta|rc)\.(?:0|[1-9][0-9]*))?$')]
    [string]$Version = '0.4.0-beta.4',

    [string]$InnoCompiler = '',

    [string]$BuildRoot = '',
    [string]$OutputRoot = '',
    [string]$TempRoot = '',

    [switch]$SkipTests,
    [switch]$SkipInstaller,
    [switch]$AllowUncommittedSource
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$null = $Version -match '^(\d+)\.(\d+)\.(\d+)'
$fileVersion = "$($Matches[1]).$($Matches[2]).$($Matches[3]).0"
foreach ($component in @($Matches[1], $Matches[2], $Matches[3])) {
    if ([uint64]$component -gt 65535) {
        throw "Version components must fit the Windows file-version range: $Version"
    }
}

$repoRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$nativeBuildScript = Join-Path $repoRoot 'scripts\build.ps1'
$launcherProject = Join-Path $repoRoot `
    'launcher-ui\WorldAtWarVR.Launcher\WorldAtWarVR.Launcher.csproj'
$launcherTestsProject = Join-Path $repoRoot `
    'launcher-ui\WorldAtWarVR.Launcher\WorldAtWarVR.Launcher.Core.Tests\WorldAtWarVR.Launcher.Core.Tests.csproj'
$installerBuildScript = Join-Path $repoRoot 'installer\build-installer.ps1'
$publicRepository = 'https://github.com/jplakon/WorldWarVR'

$resolvedBuildRoot = if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build-standalone'))
}
else {
    [System.IO.Path]::GetFullPath($BuildRoot)
}
$resolvedOutputRoot = if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'dist\standalone'))
}
else {
    [System.IO.Path]::GetFullPath($OutputRoot)
}

$nativeBuildRoot = Join-Path $resolvedBuildRoot 'native'
$nativeBuildDir = Join-Path $nativeBuildRoot `
    ("vs-{0}" -f $Configuration.ToLowerInvariant())
$launcherPublishDir = Join-Path $resolvedBuildRoot 'launcher-ui-publish'
$payloadDir = Join-Path $resolvedOutputRoot 'WorldWarVR'
$legacyPayloadDir = Join-Path $resolvedOutputRoot 'WorldAtWarVR'
$stagingDir = Join-Path $resolvedOutputRoot ".WorldWarVR.staging-$PID"
$launcherAssetsFile = Join-Path $repoRoot `
    'launcher-ui\WorldAtWarVR.Launcher\obj\project.assets.json'
$portableZipName = "WorldWarVR-v$Version.zip"
$portableZipPath = Join-Path $resolvedOutputRoot $portableZipName
$portableZipSidecarPath = "$portableZipPath.sha256"

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
        throw "Refusing to operate outside the fixed parent: $fullCandidate"
    }
    return $fullCandidate
}

function Remove-ValidatedDirectory {
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
    Assert-NormalDirectory -Path $validated -Description 'Generated directory'
    Remove-Item -LiteralPath $validated -Recurse -Force
}

function Assert-NormalFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description is missing: $Path"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description must not be a symbolic link: $Path"
    }
    return $item
}

function New-PortableZip {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceDirectory,

        [Parameter(Mandatory = $true)]
        [string]$DestinationPath
    )

    if ((Test-Path -LiteralPath $DestinationPath) -or
        (Test-Path -LiteralPath "$DestinationPath.sha256")) {
        throw "Portable release output already exists; use a fresh output directory: $DestinationPath"
    }

    $temporaryZip = "$DestinationPath.tmp-$PID"
    $temporarySidecar = "$DestinationPath.sha256.tmp-$PID"
    if ((Test-Path -LiteralPath $temporaryZip) -or
        (Test-Path -LiteralPath $temporarySidecar)) {
        throw "Refusing to reuse temporary portable-release paths for: $DestinationPath"
    }

    try {
        $fileStream = $null
        $archive = $null
        try {
            $fileStream = [System.IO.File]::Open(
                $temporaryZip,
                [System.IO.FileMode]::CreateNew,
                [System.IO.FileAccess]::ReadWrite,
                [System.IO.FileShare]::None)
            $archive = [System.IO.Compression.ZipArchive]::new(
                $fileStream,
                [System.IO.Compression.ZipArchiveMode]::Create,
                $false)
            $fixedTimestamp = [DateTimeOffset]::new(
                2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)

            $relativeFiles = @(
                Get-ChildItem -LiteralPath $SourceDirectory -File -Recurse -Force |
                    ForEach-Object {
                        [System.IO.Path]::GetRelativePath(
                            $SourceDirectory,
                            $_.FullName)
                    })
            [Array]::Sort($relativeFiles, [StringComparer]::Ordinal)

            foreach ($relativeFile in $relativeFiles) {
                $sourcePath = Join-Path $SourceDirectory $relativeFile
                $relative = $relativeFile.Replace('\', '/')
                $entry = $archive.CreateEntry(
                    "WorldWarVR/$relative",
                    [System.IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = $fixedTimestamp
                $input = $null
                $output = $null
                try {
                    $input = [System.IO.File]::OpenRead($sourcePath)
                    $output = $entry.Open()
                    $input.CopyTo($output)
                }
                finally {
                    if ($null -ne $output) { $output.Dispose() }
                    if ($null -ne $input) { $input.Dispose() }
                }
            }
        }
        finally {
            if ($null -ne $archive) { $archive.Dispose() }
            if ($null -ne $fileStream) { $fileStream.Dispose() }
        }

        $zipHash = (Get-FileHash -LiteralPath $temporaryZip -Algorithm SHA256).Hash
        [System.IO.File]::WriteAllText(
            $temporarySidecar,
            "$zipHash  $([System.IO.Path]::GetFileName($DestinationPath))`n",
            [System.Text.UTF8Encoding]::new($false))

        Move-Item -LiteralPath $temporaryZip -Destination $DestinationPath
        try {
            Move-Item `
                -LiteralPath $temporarySidecar `
                -Destination "$DestinationPath.sha256"
        }
        catch {
            Remove-Item -LiteralPath $DestinationPath -Force -ErrorAction SilentlyContinue
            throw
        }
    }
    catch {
        Remove-Item -LiteralPath $temporaryZip -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $temporarySidecar -Force -ErrorAction SilentlyContinue
        throw
    }
}

function Copy-NuGetLicenseFiles {
    param(
        [Parameter(Mandatory = $true)]
        [string]$AssetsFile,

        [Parameter(Mandatory = $true)]
        [string]$DestinationRoot
    )

    $null = Assert-NormalFile `
        -Path $AssetsFile `
        -Description 'Restored NuGet asset inventory'
    $assets = Get-Content -LiteralPath $AssetsFile -Raw | ConvertFrom-Json
    $packageRoots = @(
        $assets.packageFolders.PSObject.Properties |
            ForEach-Object Name)
    if ($packageRoots.Count -eq 0) {
        throw 'NuGet asset inventory contains no package roots.'
    }

    $copied = 0
    foreach ($libraryProperty in @(
            $assets.libraries.PSObject.Properties | Sort-Object Name)) {
        $libraryKey = $libraryProperty.Name
        $library = $libraryProperty.Value
        if ($library.type -ne 'package' -or
            [string]::IsNullOrWhiteSpace([string]$library.path)) {
            continue
        }

        $packageDirectory = $null
        foreach ($packageRoot in $packageRoots) {
            $candidate = Join-Path $packageRoot ([string]$library.path)
            if (Test-Path -LiteralPath $candidate -PathType Container) {
                $packageDirectory = $candidate
                break
            }
        }
        if ($null -eq $packageDirectory) {
            throw "Restored NuGet package is missing: $libraryKey"
        }

        $licenseFiles = @(
            Get-ChildItem -LiteralPath $packageDirectory -File -Force |
                Where-Object {
                    $_.Name -match `
                        '^(LICENSE|NOTICE|THIRD[- ]?PARTY[- ]?NOTICES?)(\..+)?$'
                } |
                Sort-Object Name)
        if ($licenseFiles.Count -eq 0) {
            continue
        }

        if ($libraryKey -match '^Microsoft\.WindowsAppSDK\.ML/' -and
            $licenseFiles.Name -notcontains 'ThirdPartyNotices.txt') {
            throw "Windows App SDK ML third-party notices are missing: $libraryKey"
        }

        $safeLibraryName = ($libraryKey -replace '[^A-Za-z0-9._-]', '_')
        $libraryDestination = Join-Path $DestinationRoot $safeLibraryName
        New-Item -ItemType Directory -Path $libraryDestination | Out-Null
        foreach ($licenseFile in $licenseFiles) {
            Copy-Item `
                -LiteralPath $licenseFile.FullName `
                -Destination (Join-Path $libraryDestination $licenseFile.Name)
            ++$copied
        }
    }

    if ($copied -eq 0) {
        throw 'No dependency license files were copied from restored packages.'
    }
}

function Copy-DotNetDownloadedPackageLicenseFiles {
    param(
        [Parameter(Mandatory = $true)]
        [string]$AssetsFile,

        [Parameter(Mandatory = $true)]
        [string]$DestinationRoot
    )

    $assets = Get-Content -LiteralPath $AssetsFile -Raw | ConvertFrom-Json
    $packageRoots = @(
        $assets.packageFolders.PSObject.Properties |
            ForEach-Object Name)
    $framework = @($assets.project.frameworks.PSObject.Properties)
    if ($framework.Count -ne 1) {
        throw 'Expected exactly one launcher target framework in NuGet assets.'
    }
    $downloads = @($framework[0].Value.downloadDependencies)
    if ($downloads.Count -eq 0) {
        throw 'NuGet assets contain no downloaded runtime packages.'
    }

    $copiedCoreRuntime = $false
    foreach ($download in $downloads) {
        $versionRange = ([string]$download.version).Trim()
        $versionParts = $versionRange.Trim('[', ']').Split(',')
        if ($versionParts.Count -ne 2 -or
            -not $versionParts[0].Trim().Equals(
                $versionParts[1].Trim(),
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Downloaded package is not pinned to one exact version: $($download.name) $versionRange"
        }
        $version = $versionParts[0].Trim()
        $relativePackagePath = Join-Path `
            ([string]$download.name).ToLowerInvariant() `
            $version.ToLowerInvariant()
        $packageDirectory = $null
        foreach ($packageRoot in $packageRoots) {
            $candidate = Join-Path $packageRoot $relativePackagePath
            if (Test-Path -LiteralPath $candidate -PathType Container) {
                $packageDirectory = $candidate
                break
            }
        }
        if ($null -eq $packageDirectory) {
            throw "Downloaded package directory is missing: $($download.name) $version"
        }

        $licenseFiles = @(
            Get-ChildItem -LiteralPath $packageDirectory -File -Force |
                Where-Object {
                    $_.Name -match `
                        '^(LICENSE|NOTICE|THIRD[- ]?PARTY[- ]?NOTICES?)(\..+)?$'
                } |
                Sort-Object Name)
        if ($licenseFiles.Count -eq 0) {
            continue
        }

        $destination = Join-Path `
            $DestinationRoot `
            (("{0}_{1}" -f $download.name, $version) -replace `
                '[^A-Za-z0-9._-]', '_')
        New-Item -ItemType Directory -Path $destination | Out-Null
        foreach ($licenseFile in $licenseFiles) {
            Copy-Item `
                -LiteralPath $licenseFile.FullName `
                -Destination (Join-Path $destination $licenseFile.Name)
        }
        if ($download.name -eq 'Microsoft.NETCore.App.Runtime.win-x64') {
            $copiedCoreRuntime = $true
        }
    }
    if (-not $copiedCoreRuntime) {
        throw 'The pinned x64 .NET runtime license files were not found.'
    }
}

foreach ($required in @(
        $nativeBuildScript,
        $launcherProject,
        $launcherTestsProject)) {
    $null = Assert-NormalFile -Path $required -Description 'Required build input'
}
$null = Assert-NormalFile `
    -Path $installerBuildScript `
    -Description 'Installer build script and payload validator'

foreach ($root in @($resolvedBuildRoot, $resolvedOutputRoot)) {
    if (Test-Path -LiteralPath $root) {
        Assert-NormalDirectory -Path $root -Description 'Generated output root'
    }
    else {
        New-Item -ItemType Directory -Path $root | Out-Null
    }
}

$sourceCommitOutput = @(
    & git -c "safe.directory=$repoRoot" -C $repoRoot rev-parse HEAD)
$sourceCommitExitCode = $LASTEXITCODE
if ($sourceCommitExitCode -ne 0 -or $sourceCommitOutput.Count -ne 1) {
    throw 'Could not resolve the source commit for release metadata.'
}
$sourceCommit = ([string]$sourceCommitOutput[0]).Trim()
if ($sourceCommit -notmatch '^[A-Fa-f0-9]{40}$') {
    throw 'Git returned an invalid source commit for release metadata.'
}

$workingTreeLines = @(
    & git -c "safe.directory=$repoRoot" -C $repoRoot status --porcelain)
if ($LASTEXITCODE -ne 0) {
    throw 'Could not inspect the source working tree for release metadata.'
}
$workingTreeState = if ($workingTreeLines.Count -eq 0) { 'clean' } else { 'modified' }

$releaseTag = "v$Version"
$tagTypeOutput = @(
    & git -c "safe.directory=$repoRoot" -C $repoRoot cat-file -t "refs/tags/$releaseTag" 2>$null)
$tagTypeExitCode = $LASTEXITCODE
$tagCommitOutput = @(
    & git -c "safe.directory=$repoRoot" -C $repoRoot rev-parse "refs/tags/$releaseTag^{commit}" 2>$null)
$tagCommitExitCode = $LASTEXITCODE
$tagType = if ($tagTypeOutput.Count -eq 1) {
    ([string]$tagTypeOutput[0]).Trim()
} else { '' }
$tagCommit = if ($tagCommitOutput.Count -eq 1) {
    ([string]$tagCommitOutput[0]).Trim()
} else { '' }

$submoduleStatus = @(
    & git -c "safe.directory=$repoRoot" -C $repoRoot submodule status --recursive)
if ($LASTEXITCODE -ne 0) {
    throw 'Could not inspect recursive source submodules.'
}
$submodulesReady = $true
foreach ($line in $submoduleStatus) {
    if ($line.Length -gt 0 -and $line[0] -ne ' ') {
        $submodulesReady = $false
        break
    }
}

$sourceTagVerified =
    $workingTreeState -eq 'clean' -and
    $tagTypeExitCode -eq 0 -and
    $tagType -ceq 'tag' -and
    $tagCommitExitCode -eq 0 -and
    $tagCommit -ceq $sourceCommit -and
    $submodulesReady
if (-not $sourceTagVerified -and -not $AllowUncommittedSource) {
    throw "Release packaging requires a clean tree, clean recursive submodules, and annotated tag $releaseTag at HEAD. Use -AllowUncommittedSource only for a local, non-publishable validation build."
}

$nativeBuildArguments = @{
    Configuration = $Configuration
    BuildRoot = $nativeBuildRoot
    Clean = $true
}
if ($SkipTests) {
    $nativeBuildArguments.SkipTests = $true
}
if (-not [string]::IsNullOrWhiteSpace($TempRoot)) {
    $nativeBuildArguments.TempRoot = [System.IO.Path]::GetFullPath($TempRoot)
}
& $nativeBuildScript @nativeBuildArguments

if (-not $SkipTests) {
    # A stale Roslyn/MSBuild server can retain a write handle to the shared
    # managed Core obj output after an earlier launcher build.  Stop only the
    # SDK-owned build servers before the release test gate so packaging is
    # deterministic and never depends on the state of a prior IDE/build run.
    & dotnet build-server shutdown
    if ($LASTEXITCODE -ne 0) {
        throw "Could not stop stale .NET build servers (exit code $LASTEXITCODE)."
    }
    & dotnet test `
        $launcherTestsProject `
        --configuration $Configuration `
        --runtime win-x64 `
        --verbosity minimal
    if ($LASTEXITCODE -ne 0) {
        throw "Launcher UI core tests failed with exit code $LASTEXITCODE."
    }
}

Remove-ValidatedDirectory `
    -Path $launcherPublishDir `
    -Parent $resolvedBuildRoot

$publishArguments = @(
    'publish',
    $launcherProject,
    '--configuration', $Configuration,
    '--runtime', 'win-x64',
    '--self-contained', 'true',
    '--output', $launcherPublishDir,
    '-p:Platform=x64',
    "-p:Version=$Version",
    "-p:FileVersion=$fileVersion",
    "-p:AssemblyVersion=$fileVersion",
    "-p:InformationalVersion=$Version",
    '-p:PublishReadyToRun=false',
    '-p:DebugType=None',
    '-p:DebugSymbols=false'
)
& dotnet @publishArguments
if ($LASTEXITCODE -ne 0) {
    throw "Launcher UI publish failed with exit code $LASTEXITCODE."
}
Assert-NormalDirectory `
    -Path $launcherPublishDir `
    -Description 'Launcher publish directory'

$launcherExecutable = Join-Path $launcherPublishDir 'WorldWarVR.exe'
$nativeHelper = Join-Path `
    $nativeBuildDir `
    "launcher\$Configuration\wawvr-launcher.exe"
$modDll = Join-Path `
    $nativeBuildDir `
    "src\mod\$Configuration\WorldWarVR.dll"

$fixedInputs = @(
    [pscustomobject]@{ Source = $nativeHelper; Destination = 'wawvr-launcher.exe' },
    [pscustomobject]@{ Source = $modDll; Destination = 'WorldWarVR.dll' },
    [pscustomobject]@{
        Source = Join-Path $repoRoot `
            'launcher-ui\WorldAtWarVR.Launcher\Assets\WorldAtWarVR-Icon.ico'
        Destination = 'WorldWarVR.ico'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'launcher\scripts\import_pezbot.ps1'
        Destination = 'WaWVR-PeZBOT-Import.ps1'
    },
    [pscustomobject]@{ Source = Join-Path $repoRoot 'LICENSE'; Destination = 'LICENSE' },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'THIRD-PARTY-NOTICES.md'
        Destination = 'THIRD-PARTY-NOTICES.txt'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'INSTALL.md'
        Destination = 'INSTALL.txt'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'docs\CONTROLS.md'
        Destination = 'CONTROLS.txt'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'KNOWN-ISSUES.md'
        Destination = 'KNOWN-ISSUES.txt'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'release\repository\REQUIREMENTS.md'
        Destination = 'REQUIREMENTS.txt'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'docs\OFFLINE_MULTIPLAYER.md'
        Destination = 'OFFLINE-MULTIPLAYER.txt'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'PROVENANCE.md'
        Destination = 'PROVENANCE.txt'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot `
            'launcher-ui\WorldAtWarVR.Launcher\Assets\Launcher\ASSET-PROVENANCE.md'
        Destination = 'ASSET-PROVENANCE.txt'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'CHANGELOG.md'
        Destination = 'CHANGELOG.txt'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'third_party\openxr-sdk\LICENSE'
        Destination = 'licenses\OpenXR-SDK\LICENSE.txt'
    },
    [pscustomobject]@{
        Source = Join-Path $repoRoot 'third_party\openxr-sdk\src\external\jsoncpp\LICENSE'
        Destination = 'licenses\JsonCpp\LICENSE.txt'
    }
)

$null = Assert-NormalFile `
    -Path $launcherExecutable `
    -Description 'Published World War VR launcher'
foreach ($input in $fixedInputs) {
    $null = Assert-NormalFile -Path $input.Source -Description 'Payload input'
}

$null = Assert-DirectChildPath -Candidate $payloadDir -Parent $resolvedOutputRoot
$null = Assert-DirectChildPath -Candidate $stagingDir -Parent $resolvedOutputRoot
if (Test-Path -LiteralPath $stagingDir) {
    throw "Refusing to reuse package staging directory: $stagingDir"
}

$stagingCreated = $false
try {
    New-Item -ItemType Directory -Path $stagingDir | Out-Null
    $stagingCreated = $true

    foreach ($publishedItem in @(
            Get-ChildItem `
                -LiteralPath $launcherPublishDir `
                -Recurse `
                -Force)) {
        if (($publishedItem.Attributes -band
                [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            $relative = [System.IO.Path]::GetRelativePath(
                $launcherPublishDir,
                $publishedItem.FullName)
            throw "Published launcher content contains a link or junction: $relative"
        }
    }

    foreach ($entry in Get-ChildItem -LiteralPath $launcherPublishDir -Force) {
        Copy-Item `
            -LiteralPath $entry.FullName `
            -Destination $stagingDir `
            -Recurse
    }

    foreach ($input in $fixedInputs) {
        $destination = Join-Path $stagingDir $input.Destination
        $destinationParent = [System.IO.Path]::GetDirectoryName($destination)
        if (-not (Test-Path -LiteralPath $destinationParent)) {
            New-Item -ItemType Directory -Path $destinationParent | Out-Null
        }
        Assert-NormalDirectory `
            -Path $destinationParent `
            -Description 'Payload destination directory'
        Copy-Item `
            -LiteralPath $input.Source `
            -Destination $destination
    }

    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    $installTextPath = Join-Path $stagingDir 'INSTALL.txt'
    $installText = [System.IO.File]::ReadAllText($installTextPath)
    $installText = $installText.Replace(
        '[release/repository/REQUIREMENTS.md](release/repository/REQUIREMENTS.md)',
        'REQUIREMENTS.txt')
    $installText = $installText.Replace(
        '[docs/OFFLINE_MULTIPLAYER.md](docs/OFFLINE_MULTIPLAYER.md)',
        'OFFLINE-MULTIPLAYER.txt')
    $installText = $installText.Replace(
        '[KNOWN-ISSUES.md](KNOWN-ISSUES.md)',
        'KNOWN-ISSUES.txt')
    [System.IO.File]::WriteAllText($installTextPath, $installText, $utf8NoBom)

    $noticesTextPath = Join-Path $stagingDir 'THIRD-PARTY-NOTICES.txt'
    $noticesText = [System.IO.File]::ReadAllText($noticesTextPath)
    $noticesText = $noticesText.Replace(
        '`PROVENANCE.md`',
        '`PROVENANCE.txt`')
    $noticesText = $noticesText.Replace(
        '`launcher-ui/WorldAtWarVR.Launcher/Assets/Launcher/ASSET-PROVENANCE.md`',
        '`ASSET-PROVENANCE.txt`')
    [System.IO.File]::WriteAllText(
        $noticesTextPath,
        $noticesText,
        $utf8NoBom)

    Copy-NuGetLicenseFiles `
        -AssetsFile $launcherAssetsFile `
        -DestinationRoot (Join-Path $stagingDir 'licenses\nuget')
    Copy-DotNetDownloadedPackageLicenseFiles `
        -AssetsFile $launcherAssetsFile `
        -DestinationRoot (Join-Path $stagingDir 'licenses\dotnet')

    $sourceText = [System.Collections.Generic.List[string]]::new()
    $sourceText.Add('World War VR source information')
    $sourceText.Add('')
    $sourceText.Add("Release: $releaseTag")
    $sourceText.Add("Repository: $publicRepository")
    $sourceText.Add("Commit: $sourceCommit")
    $sourceText.Add("Working tree when packaged: $workingTreeState")
    $sourceText.Add('')
    if ($sourceTagVerified) {
        $sourceText.Add('Obtain the exact source recursively with:')
        $sourceText.Add(
            "git clone --recursive --branch $releaseTag $publicRepository.git")
    }
    else {
        $sourceText.Add(
            'LOCAL VALIDATION BUILD ONLY: uncommitted or untagged source was explicitly allowed.')
        $sourceText.Add(
            'These binaries must not be published as a GitHub release.')
    }
    [System.IO.File]::WriteAllText(
        (Join-Path $stagingDir 'SOURCE.txt'),
        (($sourceText -join "`r`n") + "`r`n"),
        [System.Text.UTF8Encoding]::new($false))

    $dotnetVersionOutput = @(& dotnet --version)
    $dotnetVersionExitCode = $LASTEXITCODE
    if ($dotnetVersionExitCode -ne 0 -or $dotnetVersionOutput.Count -ne 1) {
        throw 'Could not record the .NET SDK version.'
    }
    $dotnetVersion = ([string]$dotnetVersionOutput[0]).Trim()
    if ([string]::IsNullOrWhiteSpace($dotnetVersion)) {
        throw 'The .NET SDK returned an empty version.'
    }

    $cmakeCommandMatch = Select-String -LiteralPath (Join-Path $nativeBuildDir 'CMakeCache.txt') -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$'
    if (@($cmakeCommandMatch).Count -ne 1) {
        throw 'Could not identify the CMake executable used by the native build.'
    }
    $nativeCmake = $cmakeCommandMatch.Matches[0].Groups[1].Value
    $cmakeVersionOutput = @(& $nativeCmake --version)
    $cmakeVersionExitCode = $LASTEXITCODE
    if ($cmakeVersionExitCode -ne 0 -or $cmakeVersionOutput.Count -eq 0) {
        throw 'Could not record the CMake version.'
    }
    $cmakeVersion = (([string]$cmakeVersionOutput[0]) -replace '^cmake version\s+', '').Trim()
    if ([string]::IsNullOrWhiteSpace($cmakeVersion)) {
        throw 'CMake returned an empty version.'
    }
    $buildInfo = @(
        'World War VR build information',
        '',
        "Product version: $Version",
        "Windows file version: $fileVersion",
        "Configuration: $Configuration",
        "Source commit: $sourceCommit",
        "Source state: $workingTreeState",
        "CMake: $cmakeVersion",
        ".NET SDK: $dotnetVersion",
        'Target: Windows 10 2004 or later; x64 launcher with x86 game proxy'
    )
    [System.IO.File]::WriteAllText(
        (Join-Path $stagingDir 'BUILD-INFO.txt'),
        (($buildInfo -join "`r`n") + "`r`n"),
        [System.Text.UTF8Encoding]::new($false))

    $prohibitedNames = @(
        'CoDWaW.exe',
        'CoDWaWmp.exe',
        't4sp.exe',
        't4mp.exe',
        'binkw32.dll',
        'PeZBOTWAW_005p.zip',
        'PROVENANCE_AUDIT.md',
        'PROVENANCE-AUDIT.md',
        'components.json',
        'WorldAtWarVR.exe',
        'WorldAtWarVR.dll',
        'WorldAtWarVR.ico',
        'WorldAtWarVR.pri'
    )
    $prohibitedExtensions = @('.ff', '.iwd', '.d3dbsp', '.bik', '.pdb')
    $payloadFiles = @(Get-ChildItem -LiteralPath $stagingDir -File -Recurse -Force)
    if ($payloadFiles.Count -eq 0) {
        throw 'Standalone payload is empty.'
    }
    foreach ($file in $payloadFiles) {
        $relative = [System.IO.Path]::GetRelativePath($stagingDir, $file.FullName)
        if (($file.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Payload file must not be a symbolic link: $relative"
        }
        if ($prohibitedNames -icontains $file.Name -or
            $file.Name -ilike 'plutonium*' -or
            $prohibitedExtensions -icontains $file.Extension) {
            throw "Forbidden game, bot, debug, or legacy file entered payload: $relative"
        }
    }

    foreach ($pdb in @(
            Get-ChildItem -LiteralPath $stagingDir -File -Recurse -Force |
                Where-Object Extension -ieq '.pdb')) {
        throw "Debug symbols entered standalone payload: $($pdb.FullName)"
    }

    $manifestLines = @(
        'World War VR standalone installer payload',
        'Payload layout: 1',
        'No Call of Duty game file or optional bot package is included.',
        '',
        'SHA-256:'
    )
    $manifestRelativePaths = @(
        Get-ChildItem -LiteralPath $stagingDir -File -Recurse -Force |
            ForEach-Object {
                [System.IO.Path]::GetRelativePath($stagingDir, $_.FullName)
            })
    [Array]::Sort($manifestRelativePaths, [StringComparer]::Ordinal)
    foreach ($manifestRelativePath in $manifestRelativePaths) {
        $manifestSourcePath = Join-Path $stagingDir $manifestRelativePath
        $relative = $manifestRelativePath.Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $manifestSourcePath -Algorithm SHA256).Hash
        $manifestLines += "$hash  $relative"
    }
    $manifestPath = Join-Path $stagingDir 'PAYLOAD-SHA256.txt'
    [System.IO.File]::WriteAllText(
        $manifestPath,
        (($manifestLines -join "`n") + "`n"),
        [System.Text.UTF8Encoding]::new($false))

    Remove-ValidatedDirectory -Path $payloadDir -Parent $resolvedOutputRoot
    Remove-ValidatedDirectory -Path $legacyPayloadDir -Parent $resolvedOutputRoot
    Move-Item -LiteralPath $stagingDir -Destination $payloadDir
    $stagingCreated = $false
}
catch {
    if ($stagingCreated -and (Test-Path -LiteralPath $stagingDir)) {
        Remove-ValidatedDirectory -Path $stagingDir -Parent $resolvedOutputRoot
    }
    throw
}

$installerArguments = @{
    PayloadDir = $payloadDir
    OutputDir = $resolvedOutputRoot
    Version = $Version
    InnoCompiler = $InnoCompiler
}
& $installerBuildScript @installerArguments -ValidateOnly

if (-not $SkipInstaller) {
    & $installerBuildScript `
        -PayloadDir $payloadDir `
        -OutputDir $resolvedOutputRoot `
        -Version $Version `
        -InnoCompiler $InnoCompiler
}

New-PortableZip `
    -SourceDirectory $payloadDir `
    -DestinationPath $portableZipPath

Write-Host "World War VR standalone payload ready: $payloadDir"
if ($SkipInstaller) {
    Write-Host 'Installer build skipped.'
}
else {
    Write-Host ("Installer ready: {0}" -f `
        (Join-Path $resolvedOutputRoot "WorldWarVR-v$Version-Setup.exe"))
}
Write-Host "Portable ZIP ready: $portableZipPath"
Write-Host "Portable ZIP checksum: $portableZipSidecarPath"
