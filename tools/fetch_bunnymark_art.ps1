# Fetches PixiJS canonical bunnymark art from britzl/defold-bunnymark (MIT-licensed).
# Idempotent: skips files that already exist with valid PNG magic bytes.
# Run ONCE before /gsd:execute-phase 50 Plan 06.
$ErrorActionPreference = 'Stop'
$out = "examples/bunnymark/raw/sd"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$pairs = @(
    @{ src = "rabbitv3.png";          dst = "bunny_red.png" }
    @{ src = "rabbitv3_ash.png";      dst = "bunny_green.png" }
    @{ src = "rabbitv3_batman.png";   dst = "bunny_blue.png" }
    @{ src = "rabbitv3_neo.png";      dst = "bunny_yellow.png" }
    @{ src = "rabbitv3_spidey.png";   dst = "bunny_purple.png" }
)
$base = "https://raw.githubusercontent.com/britzl/defold-bunnymark/master/assets/images"
$fetched = 0; $skipped = 0
foreach ($p in $pairs) {
    $dst = Join-Path $out $p.dst
    if (Test-Path $dst) {
        $bytes = Get-Content $dst -AsByteStream -TotalCount 8 -ErrorAction SilentlyContinue
        if ($bytes.Length -ge 4 -and $bytes[0] -eq 0x89 -and $bytes[1] -eq 0x50 -and $bytes[2] -eq 0x4E -and $bytes[3] -eq 0x47) {
            Write-Host "skip  $dst (already valid)"
            $skipped++; continue
        }
    }
    $url = "$base/$($p.src)"
    Write-Host "fetch $url -> $dst"
    Invoke-WebRequest -Uri $url -OutFile $dst -UseBasicParsing
    $fetched++
}
Write-Host "Fetched: $fetched, Skipped: $skipped (out of 5). Done."
