param(
  [string]$Configuration = "Release",
  [string]$BuildDir = "",
  [string]$InstallDir = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $BuildDir) { $BuildDir = Join-Path $ProjectRoot "build\windows" }
if (-not $InstallDir) { $InstallDir = Join-Path $ProjectRoot "dist\windows" }

$ConfigureArgs = @(
  "-S", $ProjectRoot,
  "-B", $BuildDir,
  "-G", "Ninja",
  "-DCMAKE_BUILD_TYPE=$Configuration",
  "-DCMAKE_C_COMPILER=clang",
  "-DCMAKE_CXX_COMPILER=clang++",
  "-DCMAKE_INSTALL_PREFIX=$InstallDir"
)

if ($env:VCPKG_ROOT) {
  $Toolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
  if (Test-Path $Toolchain) {
    $ConfigureArgs += "-DCMAKE_TOOLCHAIN_FILE=$Toolchain"
  }
}

cmake @ConfigureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with code $LASTEXITCODE" }
cmake --build $BuildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with code $LASTEXITCODE" }
cmake --install $BuildDir
if ($LASTEXITCODE -ne 0) { throw "CMake install failed with code $LASTEXITCODE" }

Write-Host "Windows package: $InstallDir"
