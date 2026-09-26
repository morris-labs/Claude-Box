# Build Claude Box for Windows.
# Run from anywhere: .\scripts\build-windows.ps1
# Installs all missing prerequisites via winget.
# Requires Windows 10 1809+ or Windows 11 (winget is built in).
#
# Run in an elevated PowerShell session (right-click -> Run as Administrator)
# for the Visual Studio Build Tools install; a normal session is enough if
# all prerequisites are already present.
param(
    [string]$QtVersion = ""  # leave blank to auto-detect; set to e.g. "6.8.3" to pin
)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoDir = Split-Path -Parent $PSScriptRoot
$GuiDir  = Join-Path $RepoDir "gui"

function Info  { param([string]$msg) Write-Host "==> $msg" -ForegroundColor Cyan }
function Ok    { param([string]$msg) Write-Host "    OK: $msg" -ForegroundColor Green }
function Fail  { param([string]$msg) Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

function Have { param([string]$cmd) return ($null -ne (Get-Command $cmd -ErrorAction SilentlyContinue)) }

# Reload both Machine and User PATH into the current session (needed after
# any winget install so newly installed tools are visible without reopening).
function Reload-Path {
    $env:PATH = ([System.Environment]::GetEnvironmentVariable("PATH", "Machine") + ";" +
                 [System.Environment]::GetEnvironmentVariable("PATH", "User")) -replace ";;", ";"
}

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
    Reload-Path
}
Ok "git $(git --version)"

# ---------------------------------------------------------------------------
# CMake
# ---------------------------------------------------------------------------
Info "Checking CMake"
if (-not (Have "cmake")) {
    Info "Installing CMake"
    winget install --id Kitware.CMake --silent --accept-package-agreements --accept-source-agreements
    Reload-Path
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

function Find-QtDir {
    param([string]$Version)
    # All roots where Qt installers commonly land (system-wide and per-user).
    $roots = @(
        "C:\Qt",
        "${env:LOCALAPPDATA}\Qt",
        "${env:USERPROFILE}\Qt",
        "C:\Program Files\Qt",
        "C:\tools\Qt"   # Chocolatey
    )
    $compilers = @("msvc2022_64", "msvc2019_64", "msvc2022_arm64")
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        # Find all version directories (x.y.z) and sort highest-first.
        $pattern = if ($Version) { $Version } else { "6.*" }
        $versions = Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' -and $_.Name -like $pattern } |
            Sort-Object { [Version]$_.Name } -Descending
        foreach ($v in $versions) {
            foreach ($comp in $compilers) {
                $candidate = Join-Path $v.FullName $comp
                if (Test-Path (Join-Path $candidate "bin\qmake.exe")) { return $candidate }
            }
        }
    }
    return $null
}

$QtDir = Find-QtDir -Version $QtVersion
if (-not $QtDir) {
    $label = if ($QtVersion) { "Qt $QtVersion" } else { "Qt 6.x" }
    Write-Host ""
    Write-Host "$label not found. Install Qt manually, then re-run:" -ForegroundColor Yellow
    Write-Host "  1. Download the Qt Online Installer: https://www.qt.io/download-qt-installer"
    Write-Host "  2. Select Qt 6.x > MSVC 2022 64-bit"
    Write-Host "  3. powershell -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1"
    Write-Host ""
    Fail "$label not installed"
}
Ok "Qt at $QtDir"
$env:PATH = "$QtDir\bin;$env:PATH"

# ---------------------------------------------------------------------------
# Inno Setup
# ---------------------------------------------------------------------------
Info "Checking Inno Setup"

function Find-Iscc {
    # 1. Already on PATH.
    $onPath = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    # 2. Search all common roots for any "Inno Setup*" subdirectory -- no
    #    version number hardcoded so any release works.
    $roots = @(
        "${env:ProgramFiles(x86)}",
        "${env:ProgramFiles}",
        "${env:LOCALAPPDATA}\Programs",
        "${env:APPDATA}\Programs"
    )
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $hit = Get-ChildItem $root -Directory -Filter "Inno Setup*" -ErrorAction SilentlyContinue |
               ForEach-Object { Join-Path $_.FullName "iscc.exe" } |
               Where-Object { Test-Path $_ } |
               Select-Object -First 1
        if ($hit) { return $hit }
    }
    return $null
}

$IsccExe = Find-Iscc
if (-not $IsccExe) {
    Info "Installing Inno Setup"
    winget install --id JRSoftware.InnoSetup --silent `
        --accept-package-agreements --accept-source-agreements
    Reload-Path
    $IsccExe = Find-Iscc
    if (-not $IsccExe) {
        Fail "Inno Setup installed but iscc.exe not found -- add its directory to PATH and re-run"
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

# Locate the produced installer without pinning the version number.
# The .iss OutputDir is ..\..\installer relative to gui\packaging\, which
# resolves to the repo root's installer\ directory.
$Installer = Get-ChildItem "$RepoDir\installer" -Filter "claude-box-setup-*.exe" -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
if ($Installer) {
    Ok "Done: $Installer"
} else {
    Fail "Installer not found after build -- check output above"
}
