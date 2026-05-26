# ci/bundle.ps1 — Create a Rivet release bundle (.zip) for Windows.
#
# Usage:
#   .\ci\bundle.ps1 -Binary build\release\rivet.exe -Version v0.1.0 -OutDir dist\
#
# Produces:
#   dist\rivet-v0.1.0-windows-x86_64.zip
#   dist\rivet-v0.1.0-windows-x86_64.zip.sha256

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Binary,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$OutDir
)

$ErrorActionPreference = "Stop"

# ─── Architecture detection ───────────────────────────────────────────────────

$Arch = switch ($env:PROCESSOR_ARCHITECTURE) {
    "AMD64" { "x86_64" }
    "ARM64" { "arm64"  }
    default { throw "Unsupported architecture: $env:PROCESSOR_ARCHITECTURE" }
}

$Triple     = "windows-$Arch"
$BundleName = "rivet-$Version-$Triple"

# ─── Git hash ─────────────────────────────────────────────────────────────────

$GitHash = if ($env:GITHUB_SHA) {
    $env:GITHUB_SHA.Substring(0, [Math]::Min(7, $env:GITHUB_SHA.Length))
} else {
    try {
        $ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
        (git -C "$ScriptDir\.." rev-parse --short HEAD 2>$null).Trim()
    } catch { "unknown" }
}
if (-not $GitHash) { $GitHash = "unknown" }

$BuildDate = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ" -AsUTC)

# ─── Stage bundle layout ──────────────────────────────────────────────────────

$TmpDir   = Join-Path $env:TEMP "rivet_bundle_$([System.Guid]::NewGuid().ToString('N').Substring(0,8))"
$StageDir = Join-Path $TmpDir $BundleName

try {
    New-Item -ItemType Directory -Force -Path (Join-Path $StageDir "bin")  | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $StageDir "meta") | Out-Null

    Copy-Item $Binary (Join-Path $StageDir "bin\rivet.exe")

    @{
        version    = $Version
        triple     = $Triple
        git_hash   = $GitHash
        build_date = $BuildDate
    } | ConvertTo-Json -Depth 2 |
        Set-Content (Join-Path $StageDir "meta\version.json") -Encoding UTF8

    @{
        os   = "windows"
        arch = $Arch
    } | ConvertTo-Json -Depth 2 |
        Set-Content (Join-Path $StageDir "meta\platform.json") -Encoding UTF8

    # ─── Package ──────────────────────────────────────────────────────────────

    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $ZipPath = Join-Path $OutDir "$BundleName.zip"
    Compress-Archive -Path $StageDir -DestinationPath $ZipPath -Force

    # ─── Checksum ─────────────────────────────────────────────────────────────

    $Hash = (Get-FileHash $ZipPath -Algorithm SHA256).Hash.ToLower()
    "$Hash  $BundleName.zip" | Set-Content "$ZipPath.sha256" -Encoding UTF8

    Write-Host "Bundle : $ZipPath"
    Write-Host "SHA256 : $ZipPath.sha256"

} finally {
    Remove-Item -Recurse -Force $TmpDir -ErrorAction SilentlyContinue
}
