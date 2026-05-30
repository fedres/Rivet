# bootstrap/install.ps1 -- Rivet one-line installer for Windows
# Usage: irm https://releases.rivet.build/install.ps1 | iex
#   Or:  $env:RIVET_VERSION="0.2.0"; irm https://releases.rivet.build/install.ps1 | iex

[CmdletBinding()]
param(
    [string]$Version  = $env:RIVET_VERSION,
    # Note: not using `??` (PS 7+ null-coalescing) -- Windows ships PS 5.1
    # by default and `irm | iex` runs in whichever shell the user invoked.
    # The if/else expression below works on every PowerShell from 5.0 up.
    [string]$HomeDir  = $(if ($env:RIVET_HOME)     { $env:RIVET_HOME }     else { Join-Path $env:USERPROFILE ".rivet" }),
    [string]$BaseUrl  = $(if ($env:RIVET_BASE_URL) { $env:RIVET_BASE_URL } else { "https://github.com/fedres/Rivet/releases/download" })
)

$ErrorActionPreference = "Stop"
$ProgressPreference    = "SilentlyContinue"   # Invoke-WebRequest is 10x faster without progress

# --- Architecture detection ---------------------------------------------------

$Arch = switch ($env:PROCESSOR_ARCHITECTURE) {
    "AMD64" { "x86_64" }
    "ARM64" { "arm64"  }
    default { throw "Unsupported architecture: $env:PROCESSOR_ARCHITECTURE" }
}

$Triple = "windows-$Arch"

# --- Windows toolchain prerequisite: none ------------------------------------
#
# Rivet ships llvm-mingw on Windows (LLVM + MinGW-w64 + libc++, built
# together by Martin Storsjo). MinGW carries the Windows headers and
# import libs we need, so unlike clang-cl / lld-link this works on a
# fresh Windows install with no Visual Studio Build Tools, no Windows
# SDK install, no winget workload prompt. Same approach Zig uses.
#
# This block used to be a ~100-line cargo-style detect-and-install
# pipeline (vswhere lookup, INCLUDE/LIB probe, winget VCTools install).
# All of it is gone now that the bundled toolchain is self-contained.

# --- Version resolution -------------------------------------------------------

if (-not $Version) {
    Write-Host "Fetching latest Rivet release..."
    try {
        $Release = Invoke-RestMethod `
            -Uri "https://api.github.com/repos/fedres/Rivet/releases/latest" `
            -Headers @{ "User-Agent" = "rivet-installer/1.0" }
        $Version = $Release.tag_name
    } catch {
        throw "Could not determine latest Rivet version. Set `$env:RIVET_VERSION and retry."
    }
}

if (-not $Version) {
    throw "Could not determine latest Rivet version."
}

$BundleName = "rivet-$Version-$Triple"
$Archive    = "$BundleName.zip"
$BundleUrl  = "$BaseUrl/$Version/$Archive"
$ShaUrl     = "$BundleUrl.sha256"

Write-Host "Installing Rivet $Version ($Triple) -> $HomeDir ..."

# --- Download -----------------------------------------------------------------

$TmpDir = Join-Path $env:TEMP "rivet_install_$([System.Guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Force -Path $TmpDir | Out-Null

try {
    $ZipPath = Join-Path $TmpDir $Archive
    Write-Host "Downloading $BundleUrl ..."
    Invoke-WebRequest -Uri $BundleUrl -OutFile $ZipPath -UseBasicParsing

    # --- Checksum verification ------------------------------------------------

    try {
        $ShaFile = Join-Path $TmpDir "$Archive.sha256"
        Invoke-WebRequest -Uri $ShaUrl -OutFile $ShaFile -UseBasicParsing -ErrorAction Stop
        $Expected = ((Get-Content $ShaFile -Raw).Trim() -split '\s+')[0].ToLower()
        $Actual   = (Get-FileHash $ZipPath -Algorithm SHA256).Hash.ToLower()
        if ($Expected -ne $Actual) {
            throw "Checksum mismatch!`n  Expected: $Expected`n  Actual:   $Actual`nThe download may be corrupt or tampered with."
        }
        Write-Host "Checksum verified."
    } catch [System.Net.WebException] {
        Write-Warning "Checksum file not available -- skipping verification."
    }

    # --- Extract --------------------------------------------------------------

    New-Item -ItemType Directory -Force -Path $HomeDir | Out-Null
    Expand-Archive -Path $ZipPath -DestinationPath $TmpDir -Force

    # Bundle unpacks to a versioned subdir -- move contents into $HomeDir.
    $Extracted = Join-Path $TmpDir $BundleName
    if (Test-Path $Extracted) {
        Copy-Item -Recurse -Force "$Extracted\*" $HomeDir
    } else {
        # Fallback: bundle root is already the content.
        Expand-Archive -Path $ZipPath -DestinationPath $HomeDir -Force
    }

    # --- PATH update ---------------------------------------------------------

    $BinDir  = Join-Path $HomeDir "bin"
    $UserPath = [Environment]::GetEnvironmentVariable("PATH", "User")
    if (-not $UserPath) { $UserPath = "" }
    $PathParts = $UserPath -split ";" | Where-Object { $_ -ne "" }

    if ($BinDir -notin $PathParts) {
        $NewPath = ($PathParts + $BinDir) -join ";"
        [Environment]::SetEnvironmentVariable("PATH", $NewPath, "User")
        Write-Host "Added $BinDir to user PATH."
        Write-Host "Restart your terminal for the change to take effect."
    }

    # --- LLVM toolchain bootstrap --------------------------------------------
    # Published by publish-toolchain.yml. Set $env:RIVET_SKIP_TOOLCHAIN=1 to
    # skip; $env:RIVET_LLVM_VERSION overrides the default version.

    if ($env:RIVET_SKIP_TOOLCHAIN -ne "1") {
        $LlvmVersion = if ($env:RIVET_LLVM_VERSION) { $env:RIVET_LLVM_VERSION } else { "19.1.7" }
        $RivetExe    = Join-Path $BinDir "rivet.exe"
        # Detect a pre-existing toolchain to avoid re-downloading on re-runs.
        $HasToolchain = $false
        try {
            $tcList = & $RivetExe toolchain list 2>$null
            if ($LASTEXITCODE -eq 0 -and ($tcList -join "`n") -match 'clang-') { $HasToolchain = $true }
        } catch {}

        if ($HasToolchain) {
            Write-Host ""
            Write-Host "Toolchain already installed -- skipping bootstrap."
        } else {
            Write-Host ""
            Write-Host "Installing LLVM toolchain $LlvmVersion ..."
            Write-Host "  (set `$env:RIVET_SKIP_TOOLCHAIN=1 to skip)"
            try {
                & $RivetExe toolchain install $LlvmVersion
                if ($LASTEXITCODE -ne 0) {
                    Write-Warning "Toolchain install failed. Retry: rivet toolchain install $LlvmVersion"
                }
            } catch {
                Write-Warning "Toolchain install failed: $_"
            }
        }
    }

    # --- Done -----------------------------------------------------------------

    Write-Host ""
    Write-Host "Rivet $Version installed to $HomeDir"
    Write-Host "Run: rivet --version"

} finally {
    Remove-Item -Recurse -Force $TmpDir -ErrorAction SilentlyContinue
}
