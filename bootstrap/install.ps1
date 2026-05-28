# bootstrap/install.ps1 — Rivet one-line installer for Windows
# Usage: irm https://releases.rivet.build/install.ps1 | iex
#   Or:  $env:RIVET_VERSION="0.2.0"; irm https://releases.rivet.build/install.ps1 | iex

[CmdletBinding()]
param(
    [string]$Version  = $env:RIVET_VERSION,
    # Note: not using `??` (PS 7+ null-coalescing) — Windows ships PS 5.1
    # by default and `irm | iex` runs in whichever shell the user invoked.
    # The if/else expression below works on every PowerShell from 5.0 up.
    [string]$HomeDir  = $(if ($env:RIVET_HOME)     { $env:RIVET_HOME }     else { Join-Path $env:USERPROFILE ".rivet" }),
    [string]$BaseUrl  = $(if ($env:RIVET_BASE_URL) { $env:RIVET_BASE_URL } else { "https://github.com/fedres/Rivet/releases/download" })
)

$ErrorActionPreference = "Stop"
$ProgressPreference    = "SilentlyContinue"   # Invoke-WebRequest is 10x faster without progress

# ─── Architecture detection ───────────────────────────────────────────────────

$Arch = switch ($env:PROCESSOR_ARCHITECTURE) {
    "AMD64" { "x86_64" }
    "ARM64" { "arm64"  }
    default { throw "Unsupported architecture: $env:PROCESSOR_ARCHITECTURE" }
}

$Triple = "windows-$Arch"

# ─── Visual Studio Build Tools detection (cargo-style) ───────────────────────
#
# Rivet ships its own clang-cl + lld-link, but Microsoft's licence forbids
# redistributing the Windows SDK (kernel32.lib, the UCRT, windows.h). Cargo
# hits the same wall — `rustup-init.exe` prompts and installs Build Tools
# via the same winget workload below. We do the equivalent.
#
# Detection order: vswhere (the official MS tool, installed with VS 2017+),
# then a registry sniff for the legacy Build Tools, then the env-var
# fallback used by vcvarsall / msvc-dev-cmd. Set $env:RIVET_SKIP_VS_CHECK=1
# to bypass entirely (corporate / portable / custom toolchain setups).

function Test-VSBuildToolsInstalled {
    if ($env:RIVET_SKIP_VS_CHECK -eq "1") { return $true }

    # vswhere — installed with any VS 2017+ at a stable known path.
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $found = & $vswhere -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and $found) { return $true }
    }

    # vcvars-set env (Developer Command Prompt / ilammy/msvc-dev-cmd / CI).
    if ($env:INCLUDE -and $env:LIB -and ($env:INCLUDE -match 'Windows Kits')) {
        return $true
    }

    return $false
}

function Install-VSBuildTools {
    Write-Host ""
    Write-Host "Visual Studio Build Tools 2022 not detected." -ForegroundColor Yellow
    Write-Host "Rivet ships its own clang-cl + lld-link, but Windows links" -ForegroundColor Yellow
    Write-Host "against Microsoft's SDK (windows.h, kernel32.lib, the UCRT)" -ForegroundColor Yellow
    Write-Host "which Microsoft doesn't allow us to redistribute. Same prereq" -ForegroundColor Yellow
    Write-Host "rustup/cargo and every other C++ tool needs on Windows." -ForegroundColor Yellow
    Write-Host ""

    # Try winget first; if absent, give the user a manual hint.
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if (-not $winget) {
        Write-Host "winget not available. Install Build Tools manually from:" -ForegroundColor Yellow
        Write-Host "  https://aka.ms/vs/17/release/vs_BuildTools.exe" -ForegroundColor Yellow
        Write-Host "Then re-run this installer." -ForegroundColor Yellow
        throw "Visual Studio Build Tools required."
    }

    $auto   = ($env:RIVET_AUTO_INSTALL_VS -eq "1")
    $proceed = $auto
    if (-not $auto) {
        # If running non-interactively (piped from iex without a TTY) we
        # can't prompt — bail with instructions instead of hanging.
        $isInteractive = [Environment]::UserInteractive -and
                         -not ([Console]::IsInputRedirected)
        if ($isInteractive) {
            $ans = Read-Host "Install via winget now? [Y/n]"
            $proceed = ($ans -eq "" -or $ans -ieq "y" -or $ans -ieq "yes")
        } else {
            Write-Host "Re-run with `$env:RIVET_AUTO_INSTALL_VS=1 to install automatically, or run:" -ForegroundColor Yellow
            Write-Host "  winget install --id Microsoft.VisualStudio.2022.BuildTools ``" -ForegroundColor Yellow
            Write-Host "    --override `"--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended`"" -ForegroundColor Yellow
            throw "Visual Studio Build Tools required (non-interactive)."
        }
    }

    if (-not $proceed) {
        throw "Build Tools install declined. Re-run after installing manually."
    }

    Write-Host "Installing Microsoft.VisualStudio.2022.BuildTools via winget ..."
    Write-Host "(this is ~6 GB and may take several minutes)"
    & winget install --id Microsoft.VisualStudio.2022.BuildTools `
        --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" `
        --accept-source-agreements --accept-package-agreements
    if ($LASTEXITCODE -ne 0) {
        throw "winget install failed (exit $LASTEXITCODE). Install manually from https://aka.ms/vs/17/release/vs_BuildTools.exe"
    }

    Write-Host ""
    Write-Host "Visual Studio Build Tools installed." -ForegroundColor Green
    Write-Host "You may need to open a new 'x64 Native Tools Command Prompt for VS 2022'" -ForegroundColor Yellow
    Write-Host "(or restart your shell) before INCLUDE/LIB/PATH are populated." -ForegroundColor Yellow
}

if (-not (Test-VSBuildToolsInstalled)) {
    Install-VSBuildTools
}

# ─── Version resolution ───────────────────────────────────────────────────────

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

Write-Host "Installing Rivet $Version ($Triple) → $HomeDir ..."

# ─── Download ─────────────────────────────────────────────────────────────────

$TmpDir = Join-Path $env:TEMP "rivet_install_$([System.Guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Force -Path $TmpDir | Out-Null

try {
    $ZipPath = Join-Path $TmpDir $Archive
    Write-Host "Downloading $BundleUrl ..."
    Invoke-WebRequest -Uri $BundleUrl -OutFile $ZipPath -UseBasicParsing

    # ─── Checksum verification ────────────────────────────────────────────────

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
        Write-Warning "Checksum file not available — skipping verification."
    }

    # ─── Extract ──────────────────────────────────────────────────────────────

    New-Item -ItemType Directory -Force -Path $HomeDir | Out-Null
    Expand-Archive -Path $ZipPath -DestinationPath $TmpDir -Force

    # Bundle unpacks to a versioned subdir — move contents into $HomeDir.
    $Extracted = Join-Path $TmpDir $BundleName
    if (Test-Path $Extracted) {
        Copy-Item -Recurse -Force "$Extracted\*" $HomeDir
    } else {
        # Fallback: bundle root is already the content.
        Expand-Archive -Path $ZipPath -DestinationPath $HomeDir -Force
    }

    # ─── PATH update ─────────────────────────────────────────────────────────

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

    # ─── LLVM toolchain bootstrap ────────────────────────────────────────────
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
            Write-Host "Toolchain already installed — skipping bootstrap."
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

    # ─── Done ─────────────────────────────────────────────────────────────────

    Write-Host ""
    Write-Host "Rivet $Version installed to $HomeDir"
    Write-Host "Run: rivet --version"

} finally {
    Remove-Item -Recurse -Force $TmpDir -ErrorAction SilentlyContinue
}
