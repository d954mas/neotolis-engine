param(
    [Parameter(Mandatory = $true)][string]$Example,
    [Parameter(Mandatory = $true)][string]$BaselinePreset,
    [Parameter(Mandatory = $true)][string]$SimdPreset
)

$ErrorActionPreference = "Stop"

$baseDir = Join-Path "build/examples/$Example" $BaselinePreset
$simdDir = Join-Path "build/examples/$Example" $SimdPreset
$baseWasm = Join-Path $baseDir "index.wasm"
$simdWasm = Join-Path $simdDir "index.wasm"
$simdJs = Join-Path $simdDir "index.js"
$outWasm = Join-Path $baseDir "index_simd.wasm"
$outJs = Join-Path $baseDir "index_simd.js"

if (-not (Test-Path -LiteralPath $baseWasm)) {
    throw "Missing baseline wasm: $baseWasm"
}

if (-not (Test-Path -LiteralPath $simdWasm)) {
    throw "Missing SIMD wasm: $simdWasm"
}

if (-not (Test-Path -LiteralPath $simdJs)) {
    throw "Missing SIMD JS: $simdJs"
}

Copy-Item -LiteralPath $simdWasm -Destination $outWasm -Force
Copy-Item -LiteralPath $simdJs -Destination $outJs -Force
Write-Host "Packaged SIMD: $outWasm + $outJs"
