param(
  [string]$Configuration = "Release",
  [string]$BuildDir = "",
  [string]$InstallDir = "",
  [string]$ReleaseDir = ""
)

$ErrorActionPreference = "Stop"
$PackageScript = Join-Path $PSScriptRoot "platforms\windows\package.ps1"
& $PackageScript -Configuration $Configuration -BuildDir $BuildDir `
  -InstallDir $InstallDir -ReleaseDir $ReleaseDir
exit $LASTEXITCODE
