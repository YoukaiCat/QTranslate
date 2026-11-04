# Builds the Windows release artifacts: the portable ZIP and, unless
# -SkipInstaller is passed, the Inno Setup installer.
#
# The script expects a Release build (or -SkipInstall with an existing dist):
#
#   cmake -S . -B build/cli-Release -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/cli-Release
#   ctest --test-dir build/cli-Release --output-on-failure
#   ./packaging/windows/package.ps1 -BuildDir build/cli-Release
#
# Inno Setup 6.3+ or 7 must be installed for the installer. The CI workflow
# installs it with Chocolatey.

[CmdletBinding()]
param(
    [string]$BuildDir = "build/cli-Release",
    [string]$DistDir = "build/dist",
    [string]$OutputDir = "build/artifacts",
    [string]$Version = "",
    [string]$Iscc = "",
    [string]$CMake = "",
    [switch]$SkipInstall,
    [switch]$SkipInstaller
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Resolve-RepoPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Find-Iscc {
    if ($Iscc) {
        if (Test-Path $Iscc) {
            return (Resolve-Path $Iscc).Path
        }
        throw "ISCC.exe was not found at '$Iscc'."
    }

    $command = Get-Command iscc -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 7\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    throw "Inno Setup compiler (ISCC.exe) was not found. Install Inno Setup 6.3+ or 7, or pass -Iscc."
}

function Find-CMake {
    if ($CMake) {
        if (Test-Path $CMake) {
            return (Resolve-Path $CMake).Path
        }
        throw "cmake was not found at '$CMake'."
    }

    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        "C:\Qt\Tools\CMake_64\bin\cmake.exe",
        "$env:ProgramFiles\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    throw "cmake was not found. Pass -CMake or add it to PATH."
}

if (-not $Version) {
    $cmakeLists = Get-Content (Join-Path $repoRoot "CMakeLists.txt") -Raw
    if ($cmakeLists -notmatch 'project\s*\(\s*qtranslate\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
        throw "Cannot read the project version from CMakeLists.txt."
    }
    $Version = $Matches[1]
}

$versionParts = $Version.Split('.')
if ($versionParts.Count -eq 3) {
    $versionQuad = "$Version.0"
} elseif ($versionParts.Count -eq 4) {
    $versionQuad = $Version
} else {
    throw "Unsupported version '$Version': expected X.Y.Z."
}

$buildPath = Resolve-RepoPath $BuildDir
$distPath = Resolve-RepoPath $DistDir
$outputPath = Resolve-RepoPath $OutputDir

if (-not $SkipInstall) {
    if (-not (Test-Path (Join-Path $buildPath "CMakeCache.txt"))) {
        throw "The build directory '$buildPath' does not exist or is not configured."
    }

    if (Test-Path $distPath) {
        Remove-Item -Recurse -Force $distPath
    }
    New-Item -ItemType Directory -Force -Path $distPath | Out-Null

    Write-Host "Installing qtranslate $Version into $distPath"
    $cmakePath = Find-CMake
    & $cmakePath --install $buildPath --config Release --prefix $distPath
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --install failed with exit code $LASTEXITCODE."
    }
}

$requiredFiles = @(
    "bin\qtranslate.exe",
    "bin\Qt6Core.dll",
    "bin\Qt6Gui.dll",
    "bin\Qt6Network.dll",
    "bin\Qt6Widgets.dll",
    "bin\vcruntime140.dll",
    "bin\msvcp140.dll",
    "plugins\platforms\qwindows.dll",
    "plugins\tls\qschannelbackend.dll"
)
foreach ($relative in $requiredFiles) {
    if (-not (Test-Path (Join-Path $distPath $relative))) {
        throw "The distribution is incomplete: '$relative' is missing from '$distPath'."
    }
}

if (Test-Path $outputPath) {
    Remove-Item -Recurse -Force $outputPath
}
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$zipPath = Join-Path $outputPath "qtranslate-$Version-win64.zip"
Write-Host "Creating $zipPath"
Compress-Archive -Path (Join-Path $distPath "*") -DestinationPath $zipPath -CompressionLevel Optimal
if (-not (Test-Path $zipPath)) {
    throw "The portable archive was not created."
}

$artifacts = @($zipPath)
if (-not $SkipInstaller) {
    $isccPath = Find-Iscc
    $issPath = Join-Path $repoRoot "packaging\windows\qtranslate.iss"
    if (-not (Test-Path $issPath)) {
        throw "Installer script '$issPath' is missing."
    }

    Write-Host "Compiling the installer with $isccPath"
    $isccArguments = @(
        "/DAppVersion=$Version",
        "/DAppVersionQuad=$versionQuad",
        "/DSourceDir=$distPath",
        "/DOutputDir=$outputPath",
        $issPath
    )
    & $isccPath @isccArguments
    if ($LASTEXITCODE -ne 0) {
        throw "ISCC failed with exit code $LASTEXITCODE."
    }

    $setupPath = Join-Path $outputPath "qtranslate-$Version-win64-setup.exe"
    if (-not (Test-Path $setupPath)) {
        throw "The installer was not created."
    }
    $artifacts += $setupPath
}

Write-Host ""
Write-Host "Release artifacts:"
foreach ($file in $artifacts) {
    $size = [math]::Round((Get-Item $file).Length / 1MB, 1)
    Write-Host ("  {0} ({1} MB)" -f (Split-Path -Leaf $file), $size)
}
