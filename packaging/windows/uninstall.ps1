param(
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\Ferra"),
  [switch]$KeepPath,
  [switch]$KeepLsp
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

if (-not $KeepLsp) {
  if ($env:VSCODE_EXTENSIONS_DIR) {
    $ExtensionsRoot = $env:VSCODE_EXTENSIONS_DIR
  } else {
    $ExtensionsRoot = Join-Path $env:USERPROFILE ".vscode\extensions"
  }
  $ExtensionDir = Join-Path $ExtensionsRoot "local.fe-0.0.1"
  $LegacyEFerraExtensionDir = Join-Path $ExtensionsRoot "local.efe-0.0.1"
  if (
    (Test-Path (Join-Path $ExtensionDir "server\ferra_lsp.py")) -or
    (Test-Path (Join-Path $ExtensionDir "server\eferra_lsp.py"))
  ) {
    Remove-Item -Recurse -Force $ExtensionDir
  }
  if (Test-Path (Join-Path $LegacyEFerraExtensionDir "server\ferra_lsp.py")) {
    Remove-Item -Recurse -Force $LegacyEFerraExtensionDir
  }
}

if (Test-Path $InstallDir) {
  Remove-Item -Recurse -Force $InstallDir
}

Write-Host "Ferra was removed. Open a new terminal to refresh PATH."
