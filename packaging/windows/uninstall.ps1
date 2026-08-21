param(
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\Ferra"),
  [switch]$KeepPath
)

$ErrorActionPreference = "Stop"
$BinDir = Join-Path $InstallDir "bin"

if (-not $KeepPath) {
  $UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
  $Entries = @($UserPath -split ";" | Where-Object {
    $_ -and $_.TrimEnd("\") -ine $BinDir.TrimEnd("\")
  })
  [Environment]::SetEnvironmentVariable("Path", ($Entries -join ";"), "User")
}

if (Test-Path $InstallDir) {
  Remove-Item -Recurse -Force $InstallDir
}

Write-Host "Ferra was removed. Open a new terminal to refresh PATH."
