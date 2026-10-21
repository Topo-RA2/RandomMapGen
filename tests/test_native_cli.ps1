param(
    [Parameter(Mandatory = $true)]
    [string]$RmgPath
)

$ErrorActionPreference = "Stop"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$rmg = (Resolve-Path -LiteralPath $RmgPath).Path
$runRoot = Join-Path $repoRoot ("build\native-cli-test-" + [DateTime]::UtcNow.ToString("yyyyMMddHHmmssfff"))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

function Invoke-Rmg([string[]]$Arguments) {
    & $rmg @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "rmg failed with exit code $LASTEXITCODE"
    }
}

function Invoke-RmgExpectedFailure([string[]]$Arguments) {
    $null = & $rmg @Arguments 2>&1
    if ($LASTEXITCODE -eq 0) {
        throw "rmg unexpectedly accepted invalid arguments"
    }
}

function Assert-SameFile([string]$Left, [string]$Right) {
    $leftHash = (Get-FileHash -LiteralPath $Left -Algorithm SHA256).Hash
    $rightHash = (Get-FileHash -LiteralPath $Right -Algorithm SHA256).Hash
    if ($leftHash -ne $rightHash) {
        throw "output mismatch: $Left=$leftHash $Right=$rightHash"
    }
}

function Assert-MapSections([string]$Path, [string[]]$Sections) {
    $text = Get-Content -LiteralPath $Path -Raw
    foreach ($section in $Sections) {
        if ($text -notmatch [regex]::Escape("[$section]")) {
            throw "missing [$section] section in $Path"
        }
    }
}

$versionOutput = & $rmg --version
if ($LASTEXITCODE -ne 0 -or $versionOutput -notmatch "^RandomMapGen ") {
    throw "unexpected --version output: $versionOutput"
}

Invoke-RmgExpectedFailure @(
    "--mode", "game-native", "--seed", "1", "--out",
    (Join-Path $runRoot "invalid-game-native.yrini")
)
Invoke-RmgExpectedFailure @(
    "--mode", "native", "--randomization-seed", "1", "--out",
    (Join-Path $runRoot "invalid-native.yrini")
)

$nativeCases = @(
    @{
        Name = "native-type1"
        Arguments = @(
            "--mode", "native", "--seed", "123", "--width", "80", "--height", "80",
            "--players", "2", "--map-type", "1", "--format", "yrini", "--tile-preset", "yr"
        )
    },
    @{
        Name = "native-type0"
        Arguments = @(
            "--mode", "native", "--seed", "777", "--width", "80", "--height", "80",
            "--players", "2", "--map-type", "0", "--vegetation", "4",
            "--format", "yrini-product", "--tile-preset", "yr"
        )
    }
)

foreach ($case in $nativeCases) {
    $first = Join-Path $runRoot ($case.Name + "-a.map")
    $second = Join-Path $runRoot ($case.Name + "-b.map")
    Invoke-Rmg ($case.Arguments + @("--out", $first))
    Invoke-Rmg ($case.Arguments + @("--out", $second))
    Assert-SameFile $first $second
    Assert-MapSections $first @("Header", "Map", "Waypoints", "IsoMapPack5")
    if ($case.Name -eq "native-type0") {
        Assert-MapSections $first @("Digest")
    }
}

$gameArguments = @(
    "--mode", "game-native", "--randomization-seed", "305419896",
    "--game-terrain-type", "2", "--time-of-day", "3", "--climate", "1",
    "--map-size", "2", "--resources", "2", "--players", "4",
    "--format", "yrini", "--tile-preset", "yr"
)
$gameFirst = Join-Path $runRoot "game-native-a.yrini"
$gameSecond = Join-Path $runRoot "game-native-b.yrini"
$gameOptionsFirst = Join-Path $runRoot "game-options-a.json"
$gameOptionsSecond = Join-Path $runRoot "game-options-b.json"
Invoke-Rmg ($gameArguments + @("--out", $gameFirst, "--native-options-report-out", $gameOptionsFirst))
Invoke-Rmg ($gameArguments + @("--out", $gameSecond, "--native-options-report-out", $gameOptionsSecond))
Assert-SameFile $gameFirst $gameSecond
Assert-SameFile $gameOptionsFirst $gameOptionsSecond
Assert-MapSections $gameFirst @("Header", "Map", "Waypoints", "IsoMapPack5")
$gameOptions = Get-Content -LiteralPath $gameOptionsFirst -Raw | ConvertFrom-Json
if ($null -eq $gameOptions.derived -or $null -eq $gameOptions.finalMapSeed) {
    throw "native options report is missing derived options or finalMapSeed"
}

Write-Host "native CLI regression passed: $runRoot"
