$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))

function Assert-NoPath([string]$RelativePath) {
    $path = Join-Path $repoRoot $RelativePath
    if (Test-Path -LiteralPath $path) {
        throw "native-only 仓库不允许包含: $RelativePath"
    }
}

Assert-NoPath "src/advanced"
Assert-NoPath "src/advanced_strategy.cpp"
Assert-NoPath "include/rmg/advanced"
Assert-NoPath "include/rmg/advanced_strategy.h"
Assert-NoPath "docs/advanced-generator-architecture.md"
Assert-NoPath "docs/native-rmg-analysis.md"
Assert-NoPath "docs/native-rmg-call-tree.generated.json"
Assert-NoPath "src/RA2AssetRenderer"

$codeFiles = @(
    Get-ChildItem -LiteralPath (Join-Path $repoRoot "include") -Recurse -File
    Get-ChildItem -LiteralPath (Join-Path $repoRoot "src") -Recurse -File
)

$forbiddenPatterns = @(
    "advancedMode",
    "advancedRecipe",
    "AdvancedStrategy",
    "StrategyPlanningContext",
    "TopologyStrategy",
    "SpawnStrategy",
    "ResourceStrategy",
    "ObjectiveStrategy",
    "rebuildAdvancedShorePieceTiles"
)

foreach ($pattern in $forbiddenPatterns) {
    $matches = @($codeFiles | Select-String -Pattern $pattern -SimpleMatch)
    if ($matches.Count -gt 0) {
        $locations = ($matches | ForEach-Object { "$($_.Path):$($_.LineNumber)" }) -join ", "
        throw "发现非原生策略代码 '$pattern': $locations"
    }
}

$assetDirectories = @("include", "src", "docs", "examples", "third_party", "tests")
$forbiddenExtensions = @(".mix", ".shp", ".vxl", ".pal", ".tmp", ".map")
$forbiddenAssets = @(
    $assetDirectories |
        ForEach-Object { Get-ChildItem -LiteralPath (Join-Path $repoRoot $_) -Recurse -File } |
        Where-Object {
            $forbiddenExtensions -contains $_.Extension.ToLowerInvariant()
        }
)
if ($forbiddenAssets.Count -gt 0) {
    throw "仓库包含不允许分发的游戏或地图资源: $($forbiddenAssets.FullName -join ', ')"
}

$markdownFiles = Get-ChildItem -LiteralPath $repoRoot -Recurse -File -Filter "*.md"
foreach ($file in $markdownFiles) {
    $text = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($match in [regex]::Matches($text, '\[[^\]]+\]\(([^)#]+)(?:#[^)]+)?\)')) {
        $target = $match.Groups[1].Value
        if ($target -match "^[a-z]+:" -or $target.StartsWith("#")) {
            continue
        }
        $resolved = [IO.Path]::GetFullPath((Join-Path $file.DirectoryName $target))
        if (!(Test-Path -LiteralPath $resolved)) {
            throw "Markdown 链接不存在: $($file.FullName) -> $target"
        }
    }
}

& (Join-Path $PSScriptRoot "verify-native-generator-layout.ps1")
Write-Host "native-only tree verification passed"
