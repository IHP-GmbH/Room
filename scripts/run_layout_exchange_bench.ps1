param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDir,
    [string]$WorkDir = "",
    [string]$Bench = ""
)

$ErrorActionPreference = "Stop"

if (-not $WorkDir) {
    $WorkDir = Join-Path $SourceDir "bench"
}
if (-not $Bench) {
    $Bench = "C:\Users\anton\Documents\CommonDB\build\layout_exchange_bench.exe"
}
if (-not (Test-Path $Bench)) {
    throw "Missing $Bench. Build target layout_exchange_bench first."
}
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

function Test-LfsPointer([string]$Path) {
    $head = Get-Content -Path $Path -TotalCount 1 -ErrorAction SilentlyContinue
    return $head -like "version https://git-lfs.github.com/spec/v1*"
}

$merged = Join-Path $WorkDir "merged.gds"
$parts = @(Get-ChildItem -Path $SourceDir -Filter "*.gds.part.*" -File -ErrorAction SilentlyContinue | Sort-Object Name)
if ($parts.Count -gt 0) {
    foreach ($part in $parts) {
        if (Test-LfsPointer $part.FullName) {
            throw "LFS pointer, not a GDS part: $($part.Name)"
        }
    }
    if (-not (Test-Path $merged)) {
        Write-Host "Merging $($parts.Count) GDS parts -> $merged"
        $out = [System.IO.File]::Create($merged)
        try {
            foreach ($part in $parts) {
                Write-Host "  $($part.Name) $($part.Length) bytes"
                $in = [System.IO.File]::OpenRead($part.FullName)
                try { $in.CopyTo($out) } finally { $in.Dispose() }
            }
        } finally {
            $out.Dispose()
        }
    } else {
        Write-Host "Using existing $merged"
    }
} else {
    $single = @(Get-ChildItem -Path $SourceDir -Filter "*.gds" -File -ErrorAction SilentlyContinue)
    if ($single.Count -eq 1) {
        $merged = $single[0].FullName
    } elseif (Test-Path $merged) {
        Write-Host "No parts in source; using $merged"
    } else {
        $merged = ""
    }
}

$oas = @(Get-ChildItem -Path $SourceDir -Filter "*.oas" -File -ErrorAction SilentlyContinue | Where-Object { -not (Test-LfsPointer $_.FullName) })
$oasPath = ""
if ($oas.Count -ge 1) {
    $oasPath = $oas[0].FullName
}

if (-not $merged -and -not $oasPath) {
    throw "No GDS or OAS in $SourceDir"
}

$args = @($WorkDir)
if ($merged) { $args += @("--gds", $merged) }
if ($oasPath) { $args += @("--oas", $oasPath) }
Write-Host "Running $Bench $($args -join ' ')"
& $Bench @args
exit $LASTEXITCODE
