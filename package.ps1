param(
  [string]$Configuration = "Release",
  [string]$BuildDir = "",
  [string]$InstallDir = "",
  [string]$ReleaseDir = "",
  [string]$ExtensionsRoot = "",
  [switch]$NoLspInstall
)

$ErrorActionPreference = "Stop"
$PackageScript = Join-Path $PSScriptRoot "platforms\windows\package.ps1"
& $PackageScript -Configuration $Configuration -BuildDir $BuildDir `
  -InstallDir $InstallDir -ReleaseDir $ReleaseDir `
  -ExtensionsRoot $ExtensionsRoot -NoLspInstall:$NoLspInstall
exit $LASTEXITCODE
