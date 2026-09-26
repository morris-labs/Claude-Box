# Build Claude Box for Windows.
# Run from anywhere: .\scripts\build-windows.ps1
# Installs all missing prerequisites via winget.
# Requires Windows 10 1809+ or Windows 11 (winget is built in).
#
# Run in an elevated PowerShell session (right-click -> Run as Administrator)
# for the Visual Studio Build Tools and Qt installs; a normal session is
# enough if all prerequisites are already present.
param(
    [string]$QtVersion = "6.7.3"  # adjust if a newer release is preferred
)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoDir = Split-Path -Parent $PSScriptRoot
$GuiDir  = Join-Path $RepoDir "gui"

function Info  { param([string]$msg) Write-Host "==> $msg" -ForegroundColor Cyan }
function Ok    { param([string]$msg) Write-Host "    OK: $msg" -ForegroundColor Green }
function Fail  { param([string]$msg) Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

function Have { param([string]$cmd) return ($null -ne (Get-Command $cmd -ErrorAction SilentlyContinue)) }

# ---------------------------------------------------------------------------
# winget
# ---------------------------------------------------------------------------
Info "Checking winget"
if (-not (Have "winget")) {
    Fail ("winget is not available. Install the App Installer from the Microsoft Store`n" +
          "or download it from: https://aka.ms/getwinget")
}
Ok "winget present"

# ---------------------------------------------------------------------------
# Git
# ---------------------------------------------------------------------------
Info "Checking Git"
if (-not (Have "git")) {
    Info "Installing Git"
    winget install --id Git.Git --silent --accept-package-agreements --accept-source-agreements
    # Reload PATH so git is visible in this session.
    $env:PATH = [System.Environment]::GetEnvironmentVariable("PATH", "Machine") + ";" +
                [System.Environment]::GetEnvironmentVariable("PATH", "User")
}
Ok "git $(git --version)"

# ---------------------------------------------------------------------------
# CMake
# ---------------------------------------------------------------------------
Info "Checking CMake"
if (-not (Have "cmake")) {
    Info "Installing CMake"
    winget install --id Kitware.CMake --silent --accept-package-agreements --accept-source-agreements
    $env:PATH = [System.Environment]::GetEnvironmentVariable("PATH", "Machine") + ";" +
                [System.Environment]::GetEnvironmentVariable("PATH", "User")
}
Ok "cmake $(cmake --version | Select-String '\d+\.\d+\.\d+' | ForEach-Object { $_.Matches[0].Value })"

# ---------------------------------------------------------------------------
# Visual Studio Build Tools 2022
# ---------------------------------------------------------------------------
Info "Checking MSVC (Visual Studio Build Tools)"
$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$HaveMsvc = (Test-Path $VsWhere) -and (& $VsWhere -latest -products * -requires Microsoft.VisualCpp.Tools.HostX64.TargetX64 2>$null | Select-String "installationPath")
if (-not $HaveMsvc) {
    Info "Installing Visual Studio 2022 Build Tools (this may take several minutes)"
    winget install --id Microsoft.VisualStudio.2022.BuildTools --silent `
        --accept-package-agreements --accept-source-agreements `
        --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    Ok "Visual Studio Build Tools installed -- you may need to reopen this terminal"
} else {
    Ok "MSVC present"
}

# ---------------------------------------------------------------------------
# Qt 6
# ---------------------------------------------------------------------------
Info "Checking Qt 6"
# Qt installs macdeployqt/windeployqt alongside qmake in its bin directory.
$QtBasePaths = @(
    "C:\Qt\$QtVersion\msvc2022_64",
    "C:\Qt\$QtVersion\msvc2019_64",
    "${env:LOCALAPPDATA}\Qt\$QtVersion\msvc2022_64",
    "${env:LOCALAPPDATA}\Qt\$QtVersion\msvc2019_64"
)
$QtDir = $QtBasePaths | Where-Object { Test-Path (Join-Path $_ "bin\qmake.exe") } | Select-Object -First 1

if (-not $QtDir) {
    Info "Qt $QtVersion not found in default locations"
    Info "Attempting winget install (Qt.Qt.$($QtVersion -replace '\.','_') or similar)"
    # winget package IDs for Qt vary by release; try the most likely ones.
    $Installed = $false
    foreach ($id in @("Qt.Qt.6", "io.qt.qtcreator")) {
        try {
            winget install --id $id --silent `
                --accept-package-agreements --accept-source-agreements 2>$null
            $Installed = $true
            break
        } catch { }
    }
    if (-not $Installed) {
        Write-Host ""
        Write-Host "Automatic Qt install failed. Install Qt manually:" -ForegroundColor Yellow
        Write-Host "  1. Download the Qt Online Installer from https://www.qt.io/download-qt-installer"
        Write-Host "  2. Select Qt $QtVersion > MSVC 2022 64-bit"
        Write-Host "  3. Re-run this script"
        Fail "Qt $QtVersion not installed"
    }
    # Re-probe after install.
    $QtDir = $QtBasePaths | Where-Object { Test-Path (Join-Path $_ "bin\qmake.exe") } | Select-Object -First 1
    if (-not $QtDir) {
        Fail ("Qt installed but not found at expected path. Set -QtVersion to the installed version`n" +
              "or pass -DCMAKE_PREFIX_PATH to cmake manually.")
    }
}
Ok "Qt at $QtDir"
$env:PATH = "$QtDir\bin;$env:PATH"

# ---------------------------------------------------------------------------
# Inno Setup
# ---------------------------------------------------------------------------
Info "Checking Inno Setup"
$IsccPaths = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\iscc.exe",
    "${env:ProgramFiles}\Inno Setup 6\iscc.exe"
)
$IsccExe = $IsccPaths | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $IsccExe) {
    Info "Installing Inno Setup"
    winget install --id JRSoftware.InnoSetup --silent `
        --accept-package-agreements --accept-source-agreements
    $IsccExe = $IsccPaths | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $IsccExe) {
        Fail "Inno Setup installed but iscc.exe not found at expected path"
    }
}
Ok "iscc at $IsccExe"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
Info "Configuring"
cmake -B "$GuiDir\build" "$GuiDir" `
    -DCMAKE_PREFIX_PATH="$QtDir" `
    -DCMAKE_BUILD_TYPE=Release

Info "Building (Release)"
cmake --build "$GuiDir\build" --config Release --parallel

Info "Creating installer"
& $IsccExe "$GuiDir\packaging\claude-box-gui.iss"

$Installer = "$GuiDir\installer\claude-box-setup-3.1.0.exe"
if (Test-Path $Installer) {
    Ok "Done: $Installer"
} else {
    Fail "Installer not found after build -- check output above"
}
