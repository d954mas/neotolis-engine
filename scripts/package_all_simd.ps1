param(
    [Parameter(Mandatory = $true)][string]$BaselinePreset,
    [Parameter(Mandatory = $true)][string]$SimdPreset
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$examplesFile = Join-Path $scriptDir "wasm_examples.sh"
$line = Get-Content -LiteralPath $examplesFile | Where-Object { $_ -match "^WASM_EXAMPLES=\(" } | Select-Object -First 1
if (-not $line) {
    throw "Missing WASM_EXAMPLES in $examplesFile"
}

$match = [regex]::Match($line, "^WASM_EXAMPLES=\((?<items>.*)\)$")
if (-not $match.Success) {
    throw "Could not parse WASM_EXAMPLES in $examplesFile"
}

$examples = $match.Groups["items"].Value.Split(" ", [System.StringSplitOptions]::RemoveEmptyEntries)
foreach ($example in $examples) {
    & (Join-Path $scriptDir "package_wasm_simd.ps1") $example $BaselinePreset $SimdPreset
}
