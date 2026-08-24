param(
  [string]$Configuration = "Release",
  [string]$BuildDir = "",
  [string]$ReleaseDir = "",
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\Ferra"),
  [string]$ExtensionsRoot = ""
)

$ErrorActionPreference = "Stop"

# Build a ZIP first, then install only from its extracted contents.  This keeps
# the development checkout out of the installation path and exercises the same
# installer that release users receive.
$ProjectRoot = $PSScriptRoot
$PackageScript = Join-Path $ProjectRoot "platforms\windows\package.ps1"
if (-not $ReleaseDir) { $ReleaseDir = Join-Path $ProjectRoot "release" }

# This directory is only the CMake/CPack staging prefix.  $InstallDir above is
# the user's real Ferra installation target and is touched only by install.ps1
# from the freshly produced ZIP.
$PackageInstallDir = Join-Path $ProjectRoot "dist\windows"
$PackageStartedAt = (Get-Date).ToUniversalTime()

$PackageArguments = @{
  Configuration = $Configuration
  InstallDir = $PackageInstallDir
  ReleaseDir = $ReleaseDir
  NoLspInstall = $true
}
if ($BuildDir) { $PackageArguments.BuildDir = $BuildDir }

& $PackageScript @PackageArguments
if ($LASTEXITCODE -ne 0) {
  throw "Windows package build failed with code $LASTEXITCODE"
}

$FreshSince = $PackageStartedAt.AddSeconds(-2)
$Archive = @(
  Get-ChildItem -LiteralPath $ReleaseDir -File -Filter "ferra-*-windows-*.zip" |
    Where-Object { $_.LastWriteTimeUtc -ge $FreshSince } |
    Sort-Object LastWriteTimeUtc -Descending
)[0]
if (-not $Archive) {
  throw "No fresh Windows Ferra ZIP was produced in: $ReleaseDir"
}

$UnpackDir = Join-Path ([IO.Path]::GetTempPath()) (
  "ferra-sync-" + [Guid]::NewGuid().ToString("N")
)
New-Item -ItemType Directory -Path $UnpackDir | Out-Null

try {
  Expand-Archive -LiteralPath $Archive.FullName -DestinationPath $UnpackDir -Force

  $InstallScripts = @(
    Get-ChildItem -LiteralPath $UnpackDir -File -Filter "install.ps1" -Recurse
  )
  if ($InstallScripts.Count -ne 1) {
    throw "The ZIP must contain exactly one install.ps1: $($Archive.FullName)"
  }

  Write-Host "Installing fresh package: $($Archive.FullName)"
  $InstallArguments = @{ InstallDir = $InstallDir }
  if ($ExtensionsRoot) { $InstallArguments.ExtensionsRoot = $ExtensionsRoot }
  & $InstallScripts[0].FullName @InstallArguments
  if ($LASTEXITCODE -ne 0) {
    throw "Package installer exited with code $LASTEXITCODE"
  }
} finally {
  if (Test-Path -LiteralPath $UnpackDir) {
    Remove-Item -LiteralPath $UnpackDir -Recurse -Force
  }
}

Write-Host "Ferra was rebuilt and reinstalled from its ZIP."
