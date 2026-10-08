[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidatePattern('^(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-(?:alpha|beta|rc)\.(?:0|[1-9][0-9]*))?$')]
    [string]$Version = '0.4.0-beta.4',

    [string]$BuildRoot = '',
    [string]$OutputRoot = '',
    [string]$TempRoot = '',

    [switch]$SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$standalonePackager = Join-Path $repoRoot 'scripts\package-standalone.ps1'
$payloadValidator = Join-Path $repoRoot 'installer\build-installer.ps1'
$openXrRelativePath = 'third_party/openxr-sdk'
$openXrRoot = Join-Path $repoRoot $openXrRelativePath
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

$resolvedBuildRoot = if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'build-patreon'))
}
else {
    [System.IO.Path]::GetFullPath($BuildRoot)
}
$resolvedOutputRoot = if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'dist\patreon'))
}
else {
    [System.IO.Path]::GetFullPath($OutputRoot)
}

$outerName = "WorldWarVR-v$Version-Patreon-Release.zip"
$outerPath = Join-Path $resolvedOutputRoot $outerName
$outerSidecarPath = Join-Path `
    $resolvedOutputRoot `
    "WorldWarVR-v$Version-Patreon-Release-SHA256.txt"
$workRoot = Join-Path $resolvedOutputRoot ".WorldWarVR-patreon-staging-$PID"
$standaloneOutputRoot = Join-Path $workRoot 'standalone'
$sourceStageRoot = Join-Path $workRoot 'source'
$outerStageRoot = Join-Path $workRoot 'outer'
$payloadDir = Join-Path $standaloneOutputRoot 'WorldWarVR'

$portableName = "WorldWarVR-v$Version-Portable.zip"
$portablePath = Join-Path $outerStageRoot $portableName
$portableSidecarPath = "$portablePath.sha256"
$sourceName = "WorldWarVR-v$Version-Source.zip"
$sourcePath = Join-Path $outerStageRoot $sourceName
$sourceSidecarPath = "$sourcePath.sha256"
$releaseNotesPath = Join-Path $outerStageRoot 'PATREON-RELEASE-NOTES.txt'
$gplNoticePath = Join-Path $outerStageRoot 'GPL-SOURCE-NOTICE.txt'

$maxSourceFiles = 20000
$maxSourceBytes = 2GB
$maxArchiveInputBytes = 3GB

function Assert-NormalDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Description is missing: $Path"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description must not be a junction or symbolic link: $Path"
    }
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

function Assert-SafeRelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    $normalized = $Path.Replace('/', '\')
    if ([string]::IsNullOrWhiteSpace($normalized) -or
        [System.IO.Path]::IsPathRooted($normalized) -or
        $normalized.StartsWith('\') -or
        $normalized.Contains(':') -or
        $normalized.IndexOf([char]0) -ge 0 -or
        $normalized -match '(^|\\)\.\.?($|\\)') {
        throw "$Description has an unsafe relative path: $Path"
    }
    return $normalized
}

function Test-IsSameOrDescendant {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Candidate,

        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $fullCandidate = [System.IO.Path]::GetFullPath($Candidate).TrimEnd('\')
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    return $fullCandidate.Equals(
        $fullRoot,
        [StringComparison]::OrdinalIgnoreCase) -or
        $fullCandidate.StartsWith(
            $fullRoot + '\',
            [StringComparison]::OrdinalIgnoreCase)
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

function Invoke-GitRaw {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $git = Get-Command 'git' -ErrorAction Stop
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $git.Source
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.ArgumentList.Add('-c')
    $startInfo.ArgumentList.Add("safe.directory=$WorkingDirectory")
    foreach ($argument in $Arguments) {
        $startInfo.ArgumentList.Add($argument)
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $stdout = [System.IO.MemoryStream]::new()
    try {
        if (-not $process.Start()) {
            throw 'Git did not start.'
        }
        $stdoutCopy = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
        $stderrRead = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $null = $stdoutCopy.GetAwaiter().GetResult()
        $stderr = $stderrRead.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) {
            throw "Git failed with exit code $($process.ExitCode): $($stderr.Trim())"
        }
        # Prevent PowerShell from enumerating the byte array.  Empty Git output
        # must remain a real zero-length byte[] rather than becoming $null.
        return ,$stdout.ToArray()
    }
    finally {
        $stdout.Dispose()
        $process.Dispose()
    }
}

function Invoke-GitText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $bytes = Invoke-GitRaw -WorkingDirectory $WorkingDirectory -Arguments $Arguments
    return ([System.Text.Encoding]::UTF8.GetString($bytes)).Trim()
}

function Get-GitNulPaths {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $bytes = Invoke-GitRaw -WorkingDirectory $WorkingDirectory -Arguments $Arguments
    if ($bytes.Length -eq 0) {
        return @()
    }
    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    return @($text.Split([char]0, [StringSplitOptions]::RemoveEmptyEntries))
}

function Test-GitIgnoredRelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    $git = Get-Command 'git' -ErrorAction Stop
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $git.Source
    $startInfo.WorkingDirectory = $repoRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @(
            '-c', "safe.directory=$repoRoot",
            'check-ignore', '--quiet', '--', $RelativePath.Replace('\', '/'))) {
        $startInfo.ArgumentList.Add($argument)
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) {
            throw 'Git check-ignore did not start.'
        }
        $stdoutRead = $process.StandardOutput.ReadToEndAsync()
        $stderrRead = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $null = $stdoutRead.GetAwaiter().GetResult()
        $stderr = $stderrRead.GetAwaiter().GetResult()
        if ($process.ExitCode -eq 0) {
            return $true
        }
        if ($process.ExitCode -eq 1) {
            return $false
        }
        throw "Git check-ignore failed with exit code $($process.ExitCode): $($stderr.Trim())"
    }
    finally {
        $process.Dispose()
    }
}

function Assert-GeneratedRootExcludedFromSource {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if (Test-IsSameOrDescendant -Candidate $fullPath -Root $openXrRoot) {
        throw "$Description must not be placed inside the OpenXR source checkout: $fullPath"
    }
    if (-not (Test-IsSameOrDescendant -Candidate $fullPath -Root $repoRoot)) {
        return
    }
    if ($fullPath.Equals(
            $repoRoot.TrimEnd('\'),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description must not be the repository root."
    }
    $relative = [System.IO.Path]::GetRelativePath($repoRoot, $fullPath)
    $null = Assert-SafeRelativePath `
        -Path $relative `
        -Description $Description
    # Directory-only ignore rules such as /build-*/ are evaluated reliably by
    # probing a hypothetical child even when the root does not exist yet.
    $ignoreProbe = $relative.TrimEnd('\', '/') + '/.worldwarvr-generated-probe'
    if (-not (Test-GitIgnoredRelativePath -RelativePath $ignoreProbe)) {
        throw "$Description is inside the repository but is not Git-ignored; it could contaminate the matching source snapshot: $fullPath"
    }
}

function Assert-NoSensitiveOrGeneratedSourcePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    $normalized = $RelativePath.Replace('/', '\')
    $leaf = [System.IO.Path]::GetFileName($normalized)
    $extension = [System.IO.Path]::GetExtension($leaf)
    $components = @($normalized.Split('\'))

    # Git supplies only tracked or nonignored files.  Guard the generated
    # superproject roots without rejecting legitimate source directories named
    # "build" inside the complete tracked OpenXR SDK (or source scripts such
    # as installer/build-installer.ps1).
    $prohibitedRootDirectories = @(
        '.git', '.vs', 'build', 'out', 'artifacts', 'dist', 'runtime', 'logs'
    )
    $firstComponent = $components[0]
    if ($prohibitedRootDirectories -icontains $firstComponent -or
        $firstComponent -ilike 'build-*' -or
        $firstComponent -ilike '.review-*') {
        throw "Generated or private directory entered the source snapshot: $RelativePath"
    }

    $prohibitedNames = @(
        'CoDWaW.exe', 'CoDWaWmp.exe', 't4sp.exe', 't4mp.exe', 'binkw32.dll',
        'PeZBOTWAW_005p.zip', 'mod.ff', 'PeZBOTWaW.iwd',
        '.env', 'id_rsa', 'id_dsa', 'id_ecdsa', 'id_ed25519',
        'credentials.json', 'secrets.json'
    )
    $prohibitedExtensions = @(
        '.asi', '.bik', '.d3dbsp', '.dmp', '.dll', '.env', '.exe', '.exp',
        '.ff', '.iwd', '.key', '.kdbx', '.lib', '.log', '.obj', '.p12',
        '.pdb', '.pem', '.pfx', '.tlog', '.vrmod'
    )
    if ($prohibitedNames -icontains $leaf -or
        $leaf -ilike 'plutonium*' -or
        $leaf -ilike '.env.*' -or
        $leaf -ilike 'credentials.*' -or
        $leaf -ilike 'secrets.*' -or
        $prohibitedExtensions -icontains $extension) {
        throw "Game, generated, credential, or private file entered the source snapshot: $RelativePath"
    }
}

function Get-SourceInventory {
    $entries = [System.Collections.Generic.List[object]]::new()
    $pathSet = [System.Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)

    $superPaths = Get-GitNulPaths `
        -WorkingDirectory $repoRoot `
        -Arguments @('ls-files', '--cached', '--others', '--exclude-standard', '-z')
    foreach ($gitPath in $superPaths) {
        $relative = Assert-SafeRelativePath `
            -Path $gitPath `
            -Description 'Superproject source inventory'
        if ($relative.Equals(
                $openXrRelativePath.Replace('/', '\'),
                [StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        if ($relative.StartsWith(
                $openXrRelativePath.Replace('/', '\') + '\',
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "The superproject unexpectedly inventories content inside the OpenXR gitlink: $gitPath"
        }

        $source = Join-Path $repoRoot $relative
        if (-not (Test-Path -LiteralPath $source)) {
            # A tracked deletion is part of the current snapshot by being absent.
            continue
        }
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Non-file source entry entered the superproject inventory: $relative"
        }
        Assert-NoSensitiveOrGeneratedSourcePath -RelativePath $relative
        $item = Assert-NormalFile -Path $source -Description 'Superproject source file'
        if (-not $pathSet.Add($relative)) {
            throw "Case-insensitive duplicate source path: $relative"
        }
        $entries.Add([pscustomobject]@{
                RelativePath = $relative
                SourcePath = $item.FullName
                Length = [int64]$item.Length
                Sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
            })
    }

    $openXrPaths = Get-GitNulPaths `
        -WorkingDirectory $openXrRoot `
        -Arguments @('ls-files', '--cached', '-z')
    foreach ($gitPath in $openXrPaths) {
        $submoduleRelative = Assert-SafeRelativePath `
            -Path $gitPath `
            -Description 'OpenXR source inventory'
        $relative = Join-Path $openXrRelativePath $submoduleRelative
        Assert-NoSensitiveOrGeneratedSourcePath -RelativePath $relative
        $source = Join-Path $openXrRoot $submoduleRelative
        $item = Assert-NormalFile -Path $source -Description 'Tracked OpenXR source file'
        if (-not $pathSet.Add($relative)) {
            throw "Case-insensitive duplicate source path: $relative"
        }
        $entries.Add([pscustomobject]@{
                RelativePath = $relative
                SourcePath = $item.FullName
                Length = [int64]$item.Length
                Sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
            })
    }

    if ($entries.Count -eq 0) {
        throw 'The corresponding-source inventory is empty.'
    }
    if ($entries.Count -gt $maxSourceFiles) {
        throw "The corresponding-source inventory exceeds $maxSourceFiles files: $($entries.Count)"
    }
    $totalBytes = [int64](($entries | Measure-Object -Property Length -Sum).Sum)
    if ($totalBytes -gt $maxSourceBytes) {
        throw "The corresponding-source inventory exceeds 2 GiB: $totalBytes bytes"
    }

    return @($entries | Sort-Object RelativePath)
}

function Assert-SourceInventoryUnchanged {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Before,

        [Parameter(Mandatory = $true)]
        [object[]]$After
    )

    if ($Before.Count -ne $After.Count) {
        throw 'The source inventory changed while binaries were being built; rerun after edits stop.'
    }
    for ($index = 0; $index -lt $Before.Count; ++$index) {
        $left = $Before[$index]
        $right = $After[$index]
        if (-not $left.RelativePath.Equals(
                $right.RelativePath,
                [StringComparison]::Ordinal) -or
            $left.Length -ne $right.Length -or
            -not $left.Sha256.Equals(
                $right.Sha256,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Source changed while binaries were being built: $($left.RelativePath)"
        }
    }
}

function Copy-SourceInventory {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Entries,

        [Parameter(Mandatory = $true)]
        [string]$DestinationRoot
    )

    foreach ($entry in $Entries) {
        $destination = Join-Path $DestinationRoot $entry.RelativePath
        $destinationParent = [System.IO.Path]::GetDirectoryName($destination)
        if (-not (Test-Path -LiteralPath $destinationParent)) {
            New-Item -ItemType Directory -Path $destinationParent | Out-Null
        }
        Copy-Item -LiteralPath $entry.SourcePath -Destination $destination
        $copied = Assert-NormalFile -Path $destination -Description 'Staged source file'
        if ($copied.Length -ne $entry.Length) {
            throw "Staged source size changed during copy: $($entry.RelativePath)"
        }
        $copiedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if (-not $copiedHash.Equals(
                $entry.Sha256,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Staged source hash changed during copy: $($entry.RelativePath)"
        }
    }
}

function Get-ValidatedDirectoryFileEntries {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceDirectory,

        [Parameter(Mandatory = $true)]
        [string]$ArchiveRoot,

        [Parameter(Mandatory = $true)]
        [int]$MaxFiles,

        [Parameter(Mandatory = $true)]
        [int64]$MaxBytes
    )

    Assert-NormalDirectory -Path $SourceDirectory -Description 'Archive source directory'
    $items = @(Get-ChildItem -LiteralPath $SourceDirectory -Recurse -Force)
    $entries = [System.Collections.Generic.List[object]]::new()
    $pathSet = [System.Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $totalBytes = [int64]0
    foreach ($item in $items) {
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Archive input contains a link or junction: $($item.FullName)"
        }
        if ($item.PSIsContainer) {
            continue
        }
        $relative = [System.IO.Path]::GetRelativePath(
            $SourceDirectory,
            $item.FullName).Replace('\', '/')
        $null = Assert-SafeRelativePath -Path $relative -Description 'Archive input'
        $archivePath = "$ArchiveRoot/$relative"
        if (-not $pathSet.Add($archivePath)) {
            throw "Case-insensitive duplicate archive path: $archivePath"
        }
        $totalBytes += [int64]$item.Length
        $entries.Add([pscustomobject]@{
                SourcePath = $item.FullName
                ArchivePath = $archivePath
                Length = [int64]$item.Length
            })
    }
    if ($entries.Count -eq 0) {
        throw 'Archive input is empty.'
    }
    if ($entries.Count -gt $MaxFiles) {
        throw "Archive input exceeds $MaxFiles files: $($entries.Count)"
    }
    if ($totalBytes -gt $MaxBytes) {
        throw "Archive input exceeds its size limit: $totalBytes bytes"
    }
    return @($entries | Sort-Object ArchivePath)
}

function New-StableZip {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Entries,

        [Parameter(Mandatory = $true)]
        [string]$DestinationPath
    )

    if ((Test-Path -LiteralPath $DestinationPath) -or
        (Test-Path -LiteralPath "$DestinationPath.tmp-$PID")) {
        throw "Archive output already exists: $DestinationPath"
    }
    if ($Entries.Count -eq 0) {
        throw "Refusing to create an empty archive: $DestinationPath"
    }

    $pathSet = [System.Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $totalBytes = [int64]0
    foreach ($entry in $Entries) {
        $archivePath = (Assert-SafeRelativePath `
                -Path ([string]$entry.ArchivePath) `
                -Description 'ZIP entry').Replace('\', '/')
        if (-not $pathSet.Add($archivePath)) {
            throw "Case-insensitive duplicate ZIP entry: $archivePath"
        }
        $item = Assert-NormalFile `
            -Path ([string]$entry.SourcePath) `
            -Description 'ZIP input file'
        $totalBytes += [int64]$item.Length
    }
    if ($totalBytes -gt $maxArchiveInputBytes) {
        throw "ZIP inputs exceed the 3 GiB safety limit: $totalBytes bytes"
    }

    $temporaryPath = "$DestinationPath.tmp-$PID"
    $fileStream = $null
    $archive = $null
    try {
        $fileStream = [System.IO.File]::Open(
            $temporaryPath,
            [System.IO.FileMode]::CreateNew,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None)
        $archive = [System.IO.Compression.ZipArchive]::new(
            $fileStream,
            [System.IO.Compression.ZipArchiveMode]::Create,
            $false)
        $fixedTimestamp = [DateTimeOffset]::new(
            2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)

        foreach ($entrySpec in @($Entries | Sort-Object ArchivePath)) {
            $archivePath = ([string]$entrySpec.ArchivePath).Replace('\', '/')
            $entry = $archive.CreateEntry(
                $archivePath,
                [System.IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $fixedTimestamp
            $input = $null
            $output = $null
            try {
                $input = [System.IO.File]::OpenRead([string]$entrySpec.SourcePath)
                $output = $entry.Open()
                $input.CopyTo($output)
            }
            finally {
                if ($null -ne $output) { $output.Dispose() }
                if ($null -ne $input) { $input.Dispose() }
            }
        }
    }
    catch {
        Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
        throw
    }
    finally {
        if ($null -ne $archive) { $archive.Dispose() }
        if ($null -ne $fileStream) { $fileStream.Dispose() }
    }

    Move-Item -LiteralPath $temporaryPath -Destination $DestinationPath
}

function Write-Sha256Sidecar {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string]$SidecarPath
    )

    if (Test-Path -LiteralPath $SidecarPath) {
        throw "Checksum output already exists: $SidecarPath"
    }
    $null = Assert-NormalFile -Path $FilePath -Description 'Checksum input'
    $hash = (Get-FileHash -LiteralPath $FilePath -Algorithm SHA256).Hash
    [System.IO.File]::WriteAllText(
        $SidecarPath,
        "$hash  $([System.IO.Path]::GetFileName($FilePath))`n",
        $utf8NoBom)
    return $hash
}

function Write-PayloadChecksumManifest {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PayloadRoot
    )

    $manifestPath = Join-Path $PayloadRoot 'PAYLOAD-SHA256.txt'
    if (Test-Path -LiteralPath $manifestPath) {
        Remove-Item -LiteralPath $manifestPath -Force
    }
    $relativePaths = @(
        Get-ChildItem -LiteralPath $PayloadRoot -File -Recurse -Force |
            ForEach-Object {
                [System.IO.Path]::GetRelativePath($PayloadRoot, $_.FullName)
            })
    [Array]::Sort($relativePaths, [StringComparer]::Ordinal)
    $lines = @(
        'World War VR Patreon portable payload',
        'Payload layout: 1',
        'No Call of Duty game file or optional bot package is included.',
        '',
        'SHA-256:'
    )
    foreach ($relativePath in $relativePaths) {
        $source = Join-Path $PayloadRoot $relativePath
        $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        $lines += "$hash  $($relativePath.Replace('\', '/'))"
    }
    [System.IO.File]::WriteAllText(
        $manifestPath,
        (($lines -join "`n") + "`n"),
        $utf8NoBom)
}

foreach ($required in @($standalonePackager, $payloadValidator)) {
    $null = Assert-NormalFile -Path $required -Description 'Required packaging input'
}
Assert-NormalDirectory -Path $repoRoot -Description 'Repository root'
Assert-NormalDirectory -Path $openXrRoot -Description 'OpenXR submodule checkout'

Assert-GeneratedRootExcludedFromSource `
    -Path $resolvedBuildRoot `
    -Description 'Build root'
Assert-GeneratedRootExcludedFromSource `
    -Path $resolvedOutputRoot `
    -Description 'Patreon output root'
if (-not [string]::IsNullOrWhiteSpace($TempRoot)) {
    Assert-GeneratedRootExcludedFromSource `
        -Path ([System.IO.Path]::GetFullPath($TempRoot)) `
        -Description 'Temporary build root'
}

if (Test-Path -LiteralPath $resolvedOutputRoot) {
    Assert-NormalDirectory -Path $resolvedOutputRoot -Description 'Patreon output root'
}
else {
    New-Item -ItemType Directory -Path $resolvedOutputRoot | Out-Null
}
if ((Test-Path -LiteralPath $outerPath) -or
    (Test-Path -LiteralPath $outerSidecarPath)) {
    throw "Patreon release output already exists; use a fresh output directory: $outerPath"
}
$null = Assert-DirectChildPath -Candidate $workRoot -Parent $resolvedOutputRoot
if (Test-Path -LiteralPath $workRoot) {
    throw "Refusing to reuse Patreon package staging: $workRoot"
}

$baseCommit = Invoke-GitText -WorkingDirectory $repoRoot -Arguments @('rev-parse', 'HEAD')
if ($baseCommit -notmatch '^[A-Fa-f0-9]{40}$') {
    throw "Git returned an invalid superproject commit: $baseCommit"
}
$gitlinkLine = Invoke-GitText `
    -WorkingDirectory $repoRoot `
    -Arguments @('ls-files', '-s', '--', $openXrRelativePath)
$gitlinkMatch = [regex]::Match(
    $gitlinkLine,
    '^160000 ([A-Fa-f0-9]{40}) 0\s+third_party/openxr-sdk$')
if (-not $gitlinkMatch.Success) {
    throw 'The OpenXR checkout is not the expected tracked superproject gitlink.'
}
$pinnedOpenXrCommit = $gitlinkMatch.Groups[1].Value.ToLowerInvariant()
$openXrCommit = (Invoke-GitText `
        -WorkingDirectory $openXrRoot `
        -Arguments @('rev-parse', 'HEAD')).ToLowerInvariant()
if ($openXrCommit -ne $pinnedOpenXrCommit) {
    throw "OpenXR is not checked out at the pinned commit $pinnedOpenXrCommit (found $openXrCommit)."
}
$openXrStatus = Invoke-GitRaw `
    -WorkingDirectory $openXrRoot `
    -Arguments @('status', '--porcelain=v1', '-z', '--untracked-files=all')
if ($openXrStatus.Length -ne 0) {
    throw 'The OpenXR submodule must be clean so its source exactly matches the pinned commit.'
}
$nestedGitlinks = Invoke-GitText `
    -WorkingDirectory $openXrRoot `
    -Arguments @('ls-files', '-s')
if ($nestedGitlinks -match '(?m)^160000\s') {
    throw 'Nested OpenXR submodules are not supported by this source packager.'
}

$superprojectStatusBytes = Invoke-GitRaw `
    -WorkingDirectory $repoRoot `
    -Arguments @('status', '--porcelain=v1', '-z', '--untracked-files=all', '--ignore-submodules=none')
$snapshotState = if ($superprojectStatusBytes.Length -eq 0) { 'clean' } else { 'modified' }

$workCreated = $false
try {
    New-Item -ItemType Directory -Path $workRoot | Out-Null
    $workCreated = $true
    New-Item -ItemType Directory -Path $sourceStageRoot | Out-Null
    New-Item -ItemType Directory -Path $outerStageRoot | Out-Null

    Write-Host 'Capturing the exact corresponding-source inventory...'
    $sourceInventoryBefore = @(Get-SourceInventory)
    Copy-SourceInventory -Entries $sourceInventoryBefore -DestinationRoot $sourceStageRoot

    $snapshotLines = [System.Collections.Generic.List[string]]::new()
    $snapshotLines.Add('World War VR complete corresponding source snapshot')
    $snapshotLines.Add('')
    $snapshotLines.Add("Product version: $Version")
    $snapshotLines.Add("Base superproject commit: $baseCommit")
    $snapshotLines.Add("Superproject snapshot state: $snapshotState")
    $snapshotLines.Add("Pinned OpenXR submodule commit: $pinnedOpenXrCommit")
    $snapshotLines.Add('')
    $snapshotLines.Add('This source ZIP is the authoritative source snapshot matching the adjacent portable binary ZIP in the same Patreon release bundle.')
    $snapshotLines.Add('It contains every current tracked or nonignored untracked regular superproject file, reflects tracked deletions by omission, and contains the complete tracked OpenXR submodule source at the pinned commit.')
    $snapshotLines.Add('The base commit identifies the starting revision; when the state is modified, the bundled file contents and hashes below define the exact modified source used for this build.')
    $snapshotLines.Add('No GitHub checkout or account is required to receive this corresponding source.')
    $snapshotLines.Add('')
    $snapshotLines.Add('SHA-256 (SOURCE-SNAPSHOT.txt itself is intentionally excluded):')
    foreach ($entry in $sourceInventoryBefore) {
        $snapshotLines.Add("$($entry.Sha256)  $($entry.RelativePath.Replace('\', '/'))")
    }
    [System.IO.File]::WriteAllText(
        (Join-Path $sourceStageRoot 'SOURCE-SNAPSHOT.txt'),
        (($snapshotLines -join "`n") + "`n"),
        $utf8NoBom)

    $standaloneArguments = @{
        Configuration = $Configuration
        Version = $Version
        BuildRoot = $resolvedBuildRoot
        OutputRoot = $standaloneOutputRoot
        SkipInstaller = $true
        AllowUncommittedSource = $true
    }
    if ($SkipTests) {
        $standaloneArguments.SkipTests = $true
    }
    if (-not [string]::IsNullOrWhiteSpace($TempRoot)) {
        $standaloneArguments.TempRoot = [System.IO.Path]::GetFullPath($TempRoot)
    }
    & $standalonePackager @standaloneArguments

    Assert-NormalDirectory -Path $payloadDir -Description 'Portable payload'
    $initialPortablePath = Join-Path $standaloneOutputRoot "WorldWarVR-v$Version.zip"
    $initialPortableSidecar = "$initialPortablePath.sha256"
    $null = Assert-NormalFile `
        -Path $initialPortablePath `
        -Description 'Initial standalone ZIP'
    $null = Assert-NormalFile `
        -Path $initialPortableSidecar `
        -Description 'Initial standalone ZIP checksum'
    Remove-Item -LiteralPath $initialPortablePath -Force
    Remove-Item -LiteralPath $initialPortableSidecar -Force

    $installText = @(
        'World War VR Patreon portable release',
        '',
        "Version: $Version",
        '',
        'This package came from the Patreon release post that supplied the outer Patreon Release ZIP. The outer ZIP also contains the complete matching source ZIP, checksums for both inner ZIPs, release notes, and the GPL source notice.',
        '',
        'INSTALL',
        '1. Extract this Portable ZIP into a new empty folder.',
        '2. Do not extract it into the Call of Duty: World at War game folder.',
        '3. Run WorldWarVR.exe and select the folder containing CoDWaW.exe if it is not detected automatically.',
        '4. Start and connect the headset and controllers before launching in VR.',
        '',
        'You must provide your own legitimate Call of Duty: World at War 1.7 installation. No game executable, map, texture, audio, video, or optional PeZBOT package is included.',
        '',
        'SUPPORT DOCUMENTS INCLUDED BESIDE THE LAUNCHER',
        '- REQUIREMENTS.txt: supported Windows, headset, and connection expectations',
        '- CONTROLS.txt: motion controls and manual weapon interactions',
        '- KNOWN-ISSUES.txt: current limitations and troubleshooting',
        '- OFFLINE-MULTIPLAYER.txt: experimental local/offline multiplayer details',
        '- CHANGELOG.txt: version changes',
        '- SOURCE.txt: matching-source and license information',
        '- THIRD-PARTY-NOTICES.txt and licenses/: third-party terms',
        '',
        'Report tester feedback through the Patreon release post or the creator contact channel identified there. Include the exact version, mode/map, weapon, headset/controllers, connection method, quality preset, reproduction steps, and WorldWarVR.log if generated.',
        '',
        'The adjacent Source ZIP in the outer Patreon bundle is the exact corresponding source for this binary package. It is not required merely to play the mod.'
    )
    [System.IO.File]::WriteAllText(
        (Join-Path $payloadDir 'INSTALL.txt'),
        (($installText -join "`r`n") + "`r`n"),
        $utf8NoBom)

    $sourceText = @(
        'World War VR corresponding source information',
        '',
        "Product version: $Version",
        'Distribution channel: the Patreon release post that supplied this package',
        "Matching source archive: $sourceName",
        "Base superproject commit: $baseCommit",
        "Superproject snapshot state: $snapshotState",
        "Pinned OpenXR submodule commit: $pinnedOpenXrCommit",
        '',
        'The matching Source ZIP is included beside this Portable ZIP in the same outer Patreon Release ZIP. It contains the complete source snapshot used for this build, including modified and nonignored untracked source files and the full tracked OpenXR submodule source. No GitHub repository or account is required.',
        '',
        'World War VR first-party code is distributed under GPL-3.0-only. See LICENSE, GPL-SOURCE-NOTICE.txt in the outer bundle, THIRD-PARTY-NOTICES.txt, PROVENANCE.txt, and the licenses directory for applicable terms.'
    )
    [System.IO.File]::WriteAllText(
        (Join-Path $payloadDir 'SOURCE.txt'),
        (($sourceText -join "`r`n") + "`r`n"),
        $utf8NoBom)

    $buildInfoPath = Join-Path $payloadDir 'BUILD-INFO.txt'
    $buildInfo = [System.IO.File]::ReadAllText($buildInfoPath).TrimEnd()
    $buildInfo += "`r`nDistribution: Patreon portable bundle`r`n"
    $buildInfo += "Matching source: $sourceName`r`n"
    $buildInfo += "Base source commit: $baseCommit`r`n"
    $buildInfo += "Source snapshot state: $snapshotState`r`n"
    $buildInfo += "OpenXR source commit: $pinnedOpenXrCommit`r`n"
    [System.IO.File]::WriteAllText($buildInfoPath, $buildInfo, $utf8NoBom)

    $knownIssuesPath = Join-Path $payloadDir 'KNOWN-ISSUES.txt'
    $knownIssues = [System.IO.File]::ReadAllText($knownIssuesPath)
    $knownIssues = [regex]::Replace(
        $knownIssues,
        '(?im)^If a problem is not covered here,.*(?:\r?\n(?!#).*)?$',
        'If a problem is not covered here, report it through the Patreon release post or the creator contact channel identified there.')
    $knownIssues = $knownIssues.Replace(
        'https://github.com/jplakon/CallOfDutyWorldAtWar_VR/issues/new/choose',
        'the Patreon release post')
    $knownIssues = $knownIssues.Replace(
        'https://github.com/jplakon/CallOfDutyWorldAtWar_VR/issues',
        'the Patreon release post')
    [System.IO.File]::WriteAllText($knownIssuesPath, $knownIssues, $utf8NoBom)

    $changeLogPath = Join-Path $payloadDir 'CHANGELOG.txt'
    $changeLog = [System.IO.File]::ReadAllText($changeLogPath)
    $changeLog = $changeLog.Replace(
        'Prepared the source repository for public tester documentation and same-repository releases.',
        'Prepared the Patreon tester bundle and channel-neutral tester documentation.')
    $changeLog = $changeLog.Replace(
        'Standardized every public release on four version-matched assets: guided Setup, Setup checksum, portable ZIP, and ZIP checksum.',
        'Bundled the portable binary and complete matching source together with SHA-256 checksums and a GPL source notice.')
    [System.IO.File]::WriteAllText($changeLogPath, $changeLog, $utf8NoBom)

    $noticesPath = Join-Path $payloadDir 'THIRD-PARTY-NOTICES.txt'
    $notices = [System.IO.File]::ReadAllText($noticesPath)
    $notices = $notices.Replace('installer payload', 'portable binary payload')
    $notices = [regex]::Replace(
        $notices,
        '(?ms)^## Inno Setup\s+.*?(?=^## t4-rtx research reference)',
        '')
    [System.IO.File]::WriteAllText($noticesPath, $notices, $utf8NoBom)

    $assetProvenancePath = Join-Path $payloadDir 'ASSET-PROVENANCE.txt'
    $assetProvenance = [System.IO.File]::ReadAllText($assetProvenancePath)
    $assetProvenance = $assetProvenance.Replace(
        'installer payload',
        'portable binary payload')
    [System.IO.File]::WriteAllText(
        $assetProvenancePath,
        $assetProvenance,
        $utf8NoBom)

    $projectGitHubReference = 'github.com/jplakon/CallOfDutyWorldAtWar_VR'
    foreach ($docName in @(
            'INSTALL.txt', 'SOURCE.txt', 'BUILD-INFO.txt',
            'KNOWN-ISSUES.txt', 'CHANGELOG.txt')) {
        $docPath = Join-Path $payloadDir $docName
        if ([System.IO.File]::ReadAllText($docPath).Contains(
                $projectGitHubReference,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Patreon payload documentation still depends on the unpublished project GitHub repository: $docName"
        }
    }
    foreach ($forbiddenSetup in @(
            Get-ChildItem -LiteralPath $payloadDir -File -Recurse -Force |
                Where-Object { $_.Name -ilike '*-Setup.exe' })) {
        throw "A guided Setup executable entered the Patreon payload: $($forbiddenSetup.FullName)"
    }

    Write-PayloadChecksumManifest -PayloadRoot $payloadDir
    & $payloadValidator `
        -PayloadDir $payloadDir `
        -OutputDir $standaloneOutputRoot `
        -Version $Version `
        -ValidateOnly

    $portableEntries = @(Get-ValidatedDirectoryFileEntries `
            -SourceDirectory $payloadDir `
            -ArchiveRoot 'WorldWarVR' `
            -MaxFiles 2048 `
            -MaxBytes 1GB)
    New-StableZip -Entries $portableEntries -DestinationPath $portablePath
    $portableHash = Write-Sha256Sidecar `
        -FilePath $portablePath `
        -SidecarPath $portableSidecarPath

    $sourceInventoryAfter = @(Get-SourceInventory)
    Assert-SourceInventoryUnchanged `
        -Before $sourceInventoryBefore `
        -After $sourceInventoryAfter

    $sourceArchiveRoot = "WorldWarVR-v$Version-Source"
    $sourceEntries = @(Get-ValidatedDirectoryFileEntries `
            -SourceDirectory $sourceStageRoot `
            -ArchiveRoot $sourceArchiveRoot `
            -MaxFiles ($maxSourceFiles + 1) `
            -MaxBytes $maxSourceBytes)
    New-StableZip -Entries $sourceEntries -DestinationPath $sourcePath
    $sourceHash = Write-Sha256Sidecar `
        -FilePath $sourcePath `
        -SidecarPath $sourceSidecarPath

    $releaseNotes = @(
        'WORLD WAR VR - PATREON TESTER RELEASE',
        '',
        "Version: $Version",
        'Status: pre-release tester build',
        '',
        'CONTENTS',
        "- ${portableName}: playable portable Windows package",
        "- $portableName.sha256: portable-package checksum",
        "- ${sourceName}: complete matching source snapshot",
        "- $sourceName.sha256: source-package checksum",
        '- GPL-SOURCE-NOTICE.txt: GPL corresponding-source information',
        '',
        'VERIFY',
        'In PowerShell, run Get-FileHash on each inner ZIP with -Algorithm SHA256 and compare it with the adjacent .sha256 file.',
        '',
        'INSTALL',
        "Extract $portableName into a new empty folder, then run WorldWarVR.exe. Do not extract it into the Call of Duty: World at War game folder.",
        '',
        'You must provide a legitimate Call of Duty: World at War 1.7 installation, an OpenXR PC VR runtime, a compatible headset, and two tracked controllers. No Call of Duty game content or PeZBOT package is included.',
        '',
        'The creator completed the Campaign in VR during beta development. Later candidates receive focused regression and physical-headset testing rather than a complete Campaign replay; not every mission, weapon variant, or headset combination has been retested on this exact build. Zombies remains a supported test target. Local/offline Multiplayer remains experimental; Online Multiplayer is unsupported.',
        '',
        'Read INSTALL.txt, REQUIREMENTS.txt, CONTROLS.txt, KNOWN-ISSUES.txt, and the other support documents inside the Portable ZIP before reporting a problem. Send feedback through the Patreon release post or the creator contact channel identified there.'
    )
    $versionedPostPath = Join-Path $repoRoot "release\PATREON-POST-v$Version.md"
    if (Test-Path -LiteralPath $versionedPostPath -PathType Leaf) {
        $releaseNotes += @('', 'VERSION-SPECIFIC RELEASE POST', '',
            [System.IO.File]::ReadAllText($versionedPostPath))
    }
    [System.IO.File]::WriteAllText(
        $releaseNotesPath,
        (($releaseNotes -join "`r`n") + "`r`n"),
        $utf8NoBom)

    $gplNotice = @(
        'WORLD WAR VR - GPL CORRESPONDING SOURCE NOTICE',
        '',
        "Version: $Version",
        '',
        'World War VR first-party code is distributed under the GNU General Public License version 3 only (GPL-3.0-only). A copy of that license is included as LICENSE in both the Portable ZIP and Source ZIP.',
        'Payment for access does not reduce the rights granted by the GPL. Recipients may use, study, modify, and redistribute the covered code under GPL-3.0-only, subject to that license.',
        '',
        "The complete corresponding source for $portableName is included in this same Patreon release bundle as $sourceName. Its SHA-256 is:",
        $sourceHash,
        '',
        "Base superproject commit: $baseCommit",
        "Source snapshot state: $snapshotState",
        "Pinned OpenXR submodule commit: $pinnedOpenXrCommit",
        '',
        'When the source snapshot state is modified, SOURCE-SNAPSHOT.txt inside the Source ZIP records every included file and hash; those bundled contents are authoritative. The source package includes build scripts and the complete tracked OpenXR source at the pinned commit. No separate GitHub checkout or account is required.',
        '',
        'Third-party components and artwork remain subject to their respective notices and licenses. Call of Duty: World at War is proprietary software and is not part of this distribution.'
    )
    [System.IO.File]::WriteAllText(
        $gplNoticePath,
        (($gplNotice -join "`r`n") + "`r`n"),
        $utf8NoBom)

    $outerEntries = @(Get-ValidatedDirectoryFileEntries `
            -SourceDirectory $outerStageRoot `
            -ArchiveRoot "WorldWarVR-v$Version-Patreon-Release" `
            -MaxFiles 6 `
            -MaxBytes $maxArchiveInputBytes)
    if ($outerEntries.Count -ne 6) {
        throw "The Patreon outer bundle must contain exactly six files; found $($outerEntries.Count)."
    }
    New-StableZip -Entries $outerEntries -DestinationPath $outerPath
    $outerHash = Write-Sha256Sidecar `
        -FilePath $outerPath `
        -SidecarPath $outerSidecarPath

    Write-Host "Patreon release bundle ready: $outerPath"
    Write-Host "Patreon release checksum: $outerSidecarPath"
    Write-Host "Portable ZIP SHA-256: $portableHash"
    Write-Host "Source ZIP SHA-256: $sourceHash"
    Write-Host "Outer ZIP SHA-256: $outerHash"
    Write-Host "Source snapshot: $snapshotState at base commit $baseCommit"
}
finally {
    if ($workCreated -and (Test-Path -LiteralPath $workRoot)) {
        Remove-ValidatedDirectory -Path $workRoot -Parent $resolvedOutputRoot
    }
}
