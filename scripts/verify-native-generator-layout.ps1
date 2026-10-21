param(
    [string]$SourcePath = (Join-Path $PSScriptRoot "..\src\native_generator.cpp"),
    [string]$ExpectedExpandedSha256 = "97D8E4A173503D95305B3C32A65258D6D839189268606108810568F5ACEC6A23"
)

$ErrorActionPreference = "Stop"

function Read-NormalizedText([string]$Path) {
    return [IO.File]::ReadAllText($Path).Replace("`r`n", "`n").Replace("`r", "`n")
}

if (!(Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
    throw "native generator source not found: $SourcePath"
}

$resolvedSource = (Resolve-Path -LiteralPath $SourcePath).Path
$sourceDirectory = Split-Path -Parent $resolvedSource
$fragmentDirectory = [IO.Path]::GetFullPath((Join-Path $sourceDirectory "native_generator_detail"))
$fragmentPrefix = $fragmentDirectory + [IO.Path]::DirectorySeparatorChar
$fragmentPattern = '(?m)^#include "native_generator_detail/(?<name>[A-Za-z0-9_.-]+\.inc)"\n'
$seenFragments = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$expanded = [regex]::Replace(
    (Read-NormalizedText $resolvedSource),
    $fragmentPattern,
    {
        param($match)

        $fragmentPath = [IO.Path]::GetFullPath((Join-Path $fragmentDirectory $match.Groups["name"].Value))
        if (!$fragmentPath.StartsWith($fragmentPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "native generator fragment escaped its directory: $fragmentPath"
        }
        if (!$seenFragments.Add($fragmentPath)) {
            throw "native generator fragment included more than once: $fragmentPath"
        }
        if (!(Test-Path -LiteralPath $fragmentPath -PathType Leaf)) {
            throw "native generator fragment not found: $fragmentPath"
        }

        $fragment = Read-NormalizedText $fragmentPath
        if (!$fragment.EndsWith("`n", [StringComparison]::Ordinal)) {
            throw "native generator fragment must end with a newline: $fragmentPath"
        }
        if ([regex]::IsMatch($fragment, $fragmentPattern)) {
            throw "nested native generator fragments are not allowed: $fragmentPath"
        }
        return $fragment
    })

if (Test-Path -LiteralPath $fragmentDirectory -PathType Container) {
    $orphanedFragments = @(
        Get-ChildItem -LiteralPath $fragmentDirectory -Filter "*.inc" -File |
            Where-Object { !$seenFragments.Contains($_.FullName) } |
            Select-Object -ExpandProperty FullName
    )
    if ($orphanedFragments.Count -gt 0) {
        throw "native generator fragments are not included: $($orphanedFragments -join ', ')"
    }
}

$utf8 = [Text.UTF8Encoding]::new($false)
$sha256 = [Security.Cryptography.SHA256]::Create()
try {
    $actualSha256 = ([BitConverter]::ToString($sha256.ComputeHash($utf8.GetBytes($expanded)))).Replace("-", "")
} finally {
    $sha256.Dispose()
}

if ($actualSha256 -ne $ExpectedExpandedSha256) {
    throw "native generator expanded source changed: expected=$ExpectedExpandedSha256 actual=$actualSha256"
}

Write-Host "native-only generator expanded source unchanged: sha256=$actualSha256 fragments=$($seenFragments.Count)"
