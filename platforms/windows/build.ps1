param(
  [string]$Configuration = "Release",
  [string]$BuildDir = "",
  [string]$InstallDir = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$CustomBuildDir = [bool]$BuildDir
if (-not $BuildDir) { $BuildDir = Join-Path $ProjectRoot "build\windows" }
if (-not $InstallDir) { $InstallDir = Join-Path $ProjectRoot "dist\windows" }

# A checkout can be moved after CMake has generated this directory. The cache
# then points at the old source root and CMake refuses to configure. Recreate
# only our default disposable build directory; never delete a caller-selected
# BuildDir automatically.
$CacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path -LiteralPath $CacheFile) {
  $CacheSourceLine = Get-Content -LiteralPath $CacheFile | Where-Object {
    $_ -like "CMAKE_HOME_DIRECTORY:INTERNAL=*"
  } | Select-Object -First 1
  $CacheSource = ""
  if ($CacheSourceLine) {
    $CacheSource = $CacheSourceLine -replace '^CMAKE_HOME_DIRECTORY:INTERNAL=', ''
  }
  $NormalizedCacheSource = $CacheSource.TrimEnd([char[]]"\/").Replace("/", "\")
  $NormalizedProjectRoot = $ProjectRoot.TrimEnd([char[]]"\/").Replace("/", "\")
  if ($NormalizedCacheSource -and $NormalizedCacheSource -ine $NormalizedProjectRoot) {
    if ($CustomBuildDir) {
      throw "Build cache belongs to: $CacheSource. Remove it or choose a new BuildDir before rebuilding."
    }
    Write-Host "Recreating moved-checkout build cache: $BuildDir"
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
  }
}

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
