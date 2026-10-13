[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ArchivePath,

    [Parameter(Mandatory = $true)]
    [string]$DestinationRoot,

    [Parameter(Mandatory = $true)]
    [UInt64]$ExpectedSize,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Fa-f0-9]{32}$')]
    [string]$ExpectedMd5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$receiptName = '.wawvr-pezbot-005p.receipt'
$requiredRootFiles = [ordered]@{
    'mod.ff' = 'mod.ff'
    'pezbotwaw.iwd' = 'PeZBOTWaW.iwd'
    'pezbot.cfg' = 'pezbot.cfg'
    'pezbot_dev.cfg' = 'pezbot_dev.cfg'
}
$allowedReadMeExtensions = @('.txt', '.rtf', '.htm', '.html', '.pdf')
$maximumEntryCount = 128
$maximumTotalBytes = [UInt64](128MB)
$maximumBinaryBytes = [UInt64](64MB)
$maximumTextBytes = [UInt64](1MB)
$maximumCompressionRatio = 1000.0

function Assert-NormalItem {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileSystemInfo]$Item,

        [Parameter(Mandatory = $true)]
        [bool]$Directory,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if ($Item.PSIsContainer -ne $Directory) {
        throw "$Description has the wrong filesystem type: $($Item.FullName)"
    }
    if (($Item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description must not be a reparse point: $($Item.FullName)"
    }
}

function Get-HexDigest {
    param(
        [Parameter(Mandatory = $true)]
        [System.Security.Cryptography.HashAlgorithm]$Algorithm,

        [Parameter(Mandatory = $true)]
        [System.IO.Stream]$Stream
    )

    $digest = $Algorithm.ComputeHash($Stream)
    return ([System.BitConverter]::ToString($digest)).Replace('-', '')
}

function Get-FileSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $stream = [System.IO.File]::Open(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read)
    try {
        $algorithm = [System.Security.Cryptography.SHA256]::Create()
        try {
            return Get-HexDigest -Algorithm $algorithm -Stream $stream
        }
        finally {
            $algorithm.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Assert-SafeZipEntryAttributes {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Compression.ZipArchiveEntry]$Entry,

        [Parameter(Mandatory = $true)]
        [bool]$Directory
    )

    # ZipArchiveEntry exposes the raw 32 bits as a signed Int32. Preserve the
    # bit pattern instead of using a checked negative-to-UInt32 conversion.
    $attributes = [System.BitConverter]::ToUInt32(
        [System.BitConverter]::GetBytes([Int32]$Entry.ExternalAttributes),
        0)
    if (($attributes -band [UInt32][System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "ZIP entry carries a Windows reparse-point attribute: $($Entry.FullName)"
    }

    $unixType = ($attributes -shr 16) -band 0xF000
    if ($unixType -eq 0xA000) {
        throw "ZIP symbolic links are not allowed: $($Entry.FullName)"
    }
    if ($unixType -ne 0 -and $unixType -ne 0x4000 -and $unixType -ne 0x8000) {
        throw "ZIP entry has an unsupported Unix filesystem type: $($Entry.FullName)"
    }
    if ($Directory -and $unixType -eq 0x8000) {
        throw "ZIP directory is marked as a regular file: $($Entry.FullName)"
    }
    if (-not $Directory -and
        (($attributes -band [UInt32][System.IO.FileAttributes]::Directory) -ne 0 -or
         $unixType -eq 0x4000)) {
        throw "ZIP file is marked as a directory: $($Entry.FullName)"
    }
}

Add-Type -AssemblyName System.IO.Compression

$archiveItem = Get-Item -LiteralPath $ArchivePath -Force
Assert-NormalItem -Item $archiveItem -Directory $false -Description 'PeZBOT archive'
if ([UInt64]$archiveItem.Length -ne $ExpectedSize) {
    throw "PeZBOT archive size mismatch: $($archiveItem.Length) bytes"
}

$destinationItem = Get-Item -LiteralPath $DestinationRoot -Force
Assert-NormalItem -Item $destinationItem -Directory $true -Description 'Import destination'
if ($destinationItem.Name -notmatch '^mp_PeZBOTWAW\.import-[A-Za-z0-9-]+$') {
    throw "Import destination has an unexpected name: $($destinationItem.Name)"
}
$destinationParent = Get-Item -LiteralPath $destinationItem.Parent.FullName -Force
Assert-NormalItem -Item $destinationParent -Directory $true -Description 'Mods directory'
if ($destinationParent.Name -ine 'mods') {
    throw "Import destination must be a direct child of a mods directory"
}
if (@([System.IO.Directory]::EnumerateFileSystemEntries($destinationItem.FullName)).Count -ne 0) {
    throw 'Import destination must be empty'
}

$archiveStream = [System.IO.File]::Open(
    $archiveItem.FullName,
    [System.IO.FileMode]::Open,
    [System.IO.FileAccess]::Read,
    [System.IO.FileShare]::Read)
try {
    if ([UInt64]$archiveStream.Length -ne $ExpectedSize) {
        throw 'PeZBOT archive changed while it was being opened'
    }

    $md5 = [System.Security.Cryptography.MD5]::Create()
    try {
        $actualMd5 = Get-HexDigest -Algorithm $md5 -Stream $archiveStream
    }
    finally {
        $md5.Dispose()
    }
    if (-not $actualMd5.Equals($ExpectedMd5, [StringComparison]::OrdinalIgnoreCase)) {
        throw "PeZBOT archive MD5 mismatch: $actualMd5"
    }
    $archiveStream.Position = 0

    $archive = [System.IO.Compression.ZipArchive]::new(
        $archiveStream,
        [System.IO.Compression.ZipArchiveMode]::Read,
        $true)
    try {
        if ($archive.Entries.Count -eq 0 -or $archive.Entries.Count -gt $maximumEntryCount) {
            throw "ZIP entry count is outside the supported range: $($archive.Entries.Count)"
        }

        $seenArchiveNames = [System.Collections.Generic.HashSet[string]]::new(
            [StringComparer]::OrdinalIgnoreCase)
        $seenOutputNames = [System.Collections.Generic.HashSet[string]]::new(
            [StringComparer]::OrdinalIgnoreCase)
        $plannedFiles = [System.Collections.Generic.List[object]]::new()
        $rootPrefix = $null
        [UInt64]$totalBytes = 0

        foreach ($entry in $archive.Entries) {
            $fullName = $entry.FullName
            if ([string]::IsNullOrWhiteSpace($fullName) -or
                $fullName.IndexOf([char]0) -ge 0 -or
                $fullName.Contains('\') -or
                $fullName.Contains(':') -or
                $fullName.StartsWith('/') -or
                -not $seenArchiveNames.Add($fullName)) {
                throw "ZIP entry has an unsafe or duplicate name: $fullName"
            }

            $directory = $fullName.EndsWith('/')
            $trimmed = if ($directory) { $fullName.TrimEnd('/') } else { $fullName }
            $segments = @($trimmed.Split('/'))
            if ($segments.Count -eq 0 -or
                $segments.Where({
                    [string]::IsNullOrWhiteSpace($_) -or $_ -eq '.' -or $_ -eq '..'
                }).Count -ne 0) {
                throw "ZIP entry contains an unsafe path segment: $fullName"
            }

            # A normal archiver may emit an explicit entry for one harmless
            # wrapper directory before mp_PeZBOTWAW. It is never reproduced at
            # the destination and all later entries must use it consistently.
            if ($directory -and $segments.Count -eq 1 -and
                $segments[0] -ine 'mp_PeZBOTWAW') {
                if ($segments[0] -notmatch '^[A-Za-z0-9 _().-]{1,120}$') {
                    throw "ZIP wrapper directory has an unsafe name: $fullName"
                }
                Assert-SafeZipEntryAttributes -Entry $entry -Directory $true
                if ($null -eq $rootPrefix) {
                    $rootPrefix = $segments[0]
                }
                elseif (-not $rootPrefix.Equals(
                        $segments[0],
                        [StringComparison]::OrdinalIgnoreCase)) {
                    throw 'ZIP entries use inconsistent wrapper directories'
                }
                continue
            }

            $modIndex = if ($segments[0] -ieq 'mp_PeZBOTWAW') {
                0
            }
            elseif ($segments.Count -ge 2 -and $segments[1] -ieq 'mp_PeZBOTWAW') {
                1
            }
            else {
                -1
            }
            if ($modIndex -lt 0 -or $modIndex -gt 1) {
                throw "ZIP entry is outside the expected mp_PeZBOTWAW folder: $fullName"
            }
            $entryPrefix = if ($modIndex -eq 0) { '' } else { $segments[0] }
            if ($null -eq $rootPrefix) {
                $rootPrefix = $entryPrefix
            }
            elseif (-not $rootPrefix.Equals($entryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'ZIP entries use inconsistent wrapper directories'
            }

            Assert-SafeZipEntryAttributes -Entry $entry -Directory $directory
            if ($directory) {
                $relativeSegments = $segments.Count - $modIndex - 1
                if ($relativeSegments -gt 1 -or
                    ($relativeSegments -eq 1 -and
                     $segments[$modIndex + 1] -ine 'ReadMe')) {
                    throw "Unexpected directory in PeZBOT archive: $fullName"
                }
                continue
            }

            $relativeCount = $segments.Count - $modIndex - 1
            $outputRelative = $null
            [UInt64]$maximumBytes = $maximumTextBytes
            if ($relativeCount -eq 1) {
                $lookup = $segments[$modIndex + 1].ToLowerInvariant()
                if (-not $requiredRootFiles.Contains($lookup)) {
                    throw "Unexpected file in the PeZBOT mod root: $fullName"
                }
                $outputRelative = $requiredRootFiles[$lookup]
                $maximumBytes = if ($lookup -in @('mod.ff', 'pezbotwaw.iwd')) {
                    $maximumBinaryBytes
                }
                else {
                    $maximumTextBytes
                }
            }
            elseif ($relativeCount -eq 2 -and
                    $segments[$modIndex + 1] -ieq 'ReadMe') {
                $readMeName = $segments[$modIndex + 2]
                if ($readMeName -notmatch
                        "^[A-Za-z0-9 _().,'-]{1,120}\.(txt|rtf|htm|html|pdf)$" -or
                    $allowedReadMeExtensions -inotcontains
                        [System.IO.Path]::GetExtension($readMeName)) {
                    throw "Unexpected ReadMe document name: $fullName"
                }
                $outputRelative = 'ReadMe/' + $readMeName
            }
            else {
                throw "Unexpected nested file in PeZBOT archive: $fullName"
            }

            if (-not $seenOutputNames.Add($outputRelative)) {
                throw "ZIP entries collide at the output path: $outputRelative"
            }
            if ($entry.Length -le 0 -or [UInt64]$entry.Length -gt $maximumBytes) {
                throw "ZIP entry length is outside the safe range: $fullName"
            }
            if ($entry.CompressedLength -le 0 -or
                ([double]$entry.Length / [double]$entry.CompressedLength) -gt
                    $maximumCompressionRatio) {
                throw "ZIP entry has an unsafe compression ratio: $fullName"
            }
            $totalBytes += [UInt64]$entry.Length
            if ($totalBytes -gt $maximumTotalBytes) {
                throw 'ZIP expands beyond the allowed total size'
            }
            $plannedFiles.Add([pscustomobject]@{
                Entry = $entry
                RelativePath = $outputRelative
            })
        }

        foreach ($required in $requiredRootFiles.Values) {
            if (-not $seenOutputNames.Contains($required)) {
                throw "Required PeZBOT file is missing: $required"
            }
        }
        if (-not $plannedFiles.Where({ $_.RelativePath.StartsWith(
                    'ReadMe/', [StringComparison]::OrdinalIgnoreCase) }).Count) {
            throw 'The PeZBOT ReadMe directory is missing or empty'
        }

        $readMeRoot = Join-Path $destinationItem.FullName 'ReadMe'
        [System.IO.Directory]::CreateDirectory($readMeRoot) | Out-Null
        Assert-NormalItem `
            -Item (Get-Item -LiteralPath $readMeRoot -Force) `
            -Directory $true `
            -Description 'ReadMe destination'

        $receiptRecords = [System.Collections.Generic.List[string]]::new()
        foreach ($planned in $plannedFiles) {
            $relativeWindows = $planned.RelativePath.Replace('/', '\')
            $outputPath = [System.IO.Path]::GetFullPath(
                (Join-Path $destinationItem.FullName $relativeWindows))
            $requiredPrefix = $destinationItem.FullName.TrimEnd('\') + '\'
            if (-not $outputPath.StartsWith(
                    $requiredPrefix,
                    [StringComparison]::OrdinalIgnoreCase)) {
                throw "Planned output escaped the import root: $($planned.RelativePath)"
            }

            $source = $planned.Entry.Open()
            try {
                $output = [System.IO.File]::Open(
                    $outputPath,
                    [System.IO.FileMode]::CreateNew,
                    [System.IO.FileAccess]::Write,
                    [System.IO.FileShare]::None)
                try {
                    $source.CopyTo($output)
                }
                finally {
                    $output.Dispose()
                }
            }
            finally {
                $source.Dispose()
            }

            $outputItem = Get-Item -LiteralPath $outputPath -Force
            Assert-NormalItem -Item $outputItem -Directory $false -Description 'Extracted file'
            if ([UInt64]$outputItem.Length -ne [UInt64]$planned.Entry.Length) {
                throw "Extracted length mismatch: $($planned.RelativePath)"
            }
            $sha256 = Get-FileSha256 -Path $outputPath
            $receiptRecords.Add(
                "file|$($planned.RelativePath)|$($outputItem.Length)|$sha256")
        }

        $receiptLines = [System.Collections.Generic.List[string]]::new()
        $receiptLines.Add('WAWVR_PEZBOT_RECEIPT_V1')
        $receiptLines.Add("archive-size|$ExpectedSize")
        $receiptLines.Add("archive-md5|$($actualMd5.ToUpperInvariant())")
        foreach ($record in ($receiptRecords | Sort-Object)) {
            $receiptLines.Add($record)
        }
        $receiptText = ($receiptLines -join "`n") + "`n"
        $receiptPath = Join-Path $destinationItem.FullName $receiptName
        $receiptStream = [System.IO.File]::Open(
            $receiptPath,
            [System.IO.FileMode]::CreateNew,
            [System.IO.FileAccess]::Write,
            [System.IO.FileShare]::None)
        try {
            $receiptBytes = [System.Text.UTF8Encoding]::new($false).GetBytes($receiptText)
            $receiptStream.Write($receiptBytes, 0, $receiptBytes.Length)
            $receiptStream.Flush()
        }
        finally {
            $receiptStream.Dispose()
        }
    }
    finally {
        $archive.Dispose()
    }
}
finally {
    $archiveStream.Dispose()
}
