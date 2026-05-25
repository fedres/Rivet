# bootstrap/install.ps1 — One-line installer for Rivet on Windows
# Usage: irm https://rivet.build/install.ps1 | iex

[CmdletBinding()]
param(
    [string]$InstallDir = (Join-Path $env:USERPROFILE ".rivet\bin")
)

$ErrorActionPreference = "Stop"

$Repo = "https://github.com/fedres/Rivet"
$Api  = "https://api.github.com/repos/fedres/Rivet/releases/latest"

# ─── Detect architecture ─────────────────────────────────────────────────────

$Arch = switch ($env:PROCESSOR_ARCHITECTURE) {
    "AMD64" { "x86_64" }
    "ARM64" { "arm64"  }
    default { throw "Unsupported architecture: $env:PROCESSOR_ARCHITECTURE" }
}

$Triple = "${Arch}-windows"

# ─── Resolve latest tag ───────────────────────────────────────────────────────

Write-Host "Fetching latest Rivet release..."
$Release = Invoke-RestMethod -Uri $Api -Headers @{ "User-Agent" = "rivet-installer" }
$Tag = $Release.tag_name

if (-not $Tag) {
    throw "Could not determine latest Rivet release."
}

$Archive = "rivet-${Tag}-${Triple}.zip"
$Url     = "${Repo}/releases/download/${Tag}/${Archive}"
$ShaUrl  = "${Url}.sha256"

Write-Host "Installing Rivet ${Tag} (${Triple}) to ${InstallDir} ..."

# ─── Create install dir ───────────────────────────────────────────────────────

if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir | Out-Null
}

# ─── Download ─────────────────────────────────────────────────────────────────

$TmpDir  = Join-Path $env:TEMP "rivet_install"
New-Item -ItemType Directory -Force -Path $TmpDir | Out-Null
$ZipPath = Join-Path $TmpDir $Archive

Write-Host "Downloading $Url ..."
Invoke-WebRequest -Uri $Url -OutFile $ZipPath -UseBasicParsing

# ─── Verify checksum (if available) ──────────────────────────────────────────

try {
    $ShaFile = Join-Path $TmpDir "${Archive}.sha256"
    Invoke-WebRequest -Uri $ShaUrl -OutFile $ShaFile -UseBasicParsing -ErrorAction Stop
    $Expected = (Get-Content $ShaFile -Raw).Trim().Split()[0].ToLower()
    $Actual   = (Get-FileHash $ZipPath -Algorithm SHA256).Hash.ToLower()
    if ($Expected -ne $Actual) {
        throw "Checksum mismatch! Expected $Expected, got $Actual"
    }
    Write-Host "Checksum verified."
} catch {
    Write-Warning "Could not verify checksum (skip): $_"
}

# ─── Extract ──────────────────────────────────────────────────────────────────

Expand-Archive -Path $ZipPath -DestinationPath $InstallDir -Force

# ─── Add to user PATH ─────────────────────────────────────────────────────────

$UserPath = [Environment]::GetEnvironmentVariable("PATH", "User")
if (-not ($UserPath -split ";" | Where-Object { $_ -eq $InstallDir })) {
    [Environment]::SetEnvironmentVariable("PATH", "$UserPath;$InstallDir", "User")
    Write-Host "Added $InstallDir to your user PATH."
    Write-Host "Restart your terminal for the change to take effect."
}

# ─── Cleanup ──────────────────────────────────────────────────────────────────

Remove-Item -Recurse -Force $TmpDir

Write-Host ""
Write-Host "Rivet $Tag installed successfully!"
Write-Host "Run 'rivet --version' to verify."
