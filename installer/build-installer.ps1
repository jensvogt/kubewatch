<#
.SYNOPSIS
    Builds kubewatch and packages it into a Qt Installer Framework installer.

.DESCRIPTION
    Mirrors the manual steps CI would run: configure + build (Release),
    deploy the Qt runtime into the installer's data folder with windeployqt,
    then invoke binarycreator to produce the installer executable.

    Run from a shell with the MSVC toolchain on PATH (e.g. "x64 Native Tools
    Command Prompt for VS 2022" running powershell, or CLion's bundled terminal).
#>
param(
    [string]$Config = "Release",
    [string]$QtDir = "C:\work\private\Qt\6.10.0\msvc2022_64",
    [string]$IfwDir = "C:\work\private\Qt\Tools\QtInstallerFramework\4.10"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "cmake-build-$($Config.ToLower())"
$installerDir = Join-Path $repoRoot "installer"
$packageId = "de.jensvogt.kubewatch"
$dataDir = Join-Path $installerDir "packages\$packageId\data"
$version = (Get-Content (Join-Path $repoRoot "version.txt")).Trim()

Write-Host "Configuring CMake ($Config)..."
cmake -S $repoRoot -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=$Config -DQt6_DIR="$QtDir\lib\cmake\Qt6"

Write-Host "Building kubewatch ($Config)..."
cmake --build $buildDir --config $Config --parallel

$exe = Join-Path $buildDir "kubewatch.exe"
if (!(Test-Path $exe)) { throw "Build output not found: $exe" }

Write-Host "Deploying Qt runtime into installer data folder..."
if (Test-Path $dataDir) { Remove-Item $dataDir -Recurse -Force }
New-Item -ItemType Directory -Path $dataDir -Force | Out-Null
& "$QtDir\bin\windeployqt6.exe" $exe --dir $dataDir
Copy-Item $exe $dataDir
Copy-Item (Join-Path $repoRoot "dist\etc\kubewatch.json") $dataDir

Write-Host "Building installer with binarycreator..."
$binaryCreator = Join-Path $IfwDir "bin\binarycreator.exe"
if (!(Test-Path $binaryCreator)) { throw "binarycreator not found: $binaryCreator" }
$outputExe = Join-Path $installerDir "kubewatch-$version-windows.exe"
& $binaryCreator -c (Join-Path $installerDir "config\config.xml") -p (Join-Path $installerDir "packages") $outputExe

Write-Host "Installer created: $outputExe"
