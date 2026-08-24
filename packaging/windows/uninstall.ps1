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
  if ($env:VSCODE_EXTENSIONS) {
    $ExtensionRoots = @($env:VSCODE_EXTENSIONS)
  } elseif ($env:VSCODE_EXTENSIONS_DIR) {
    $ExtensionRoots = @($env:VSCODE_EXTENSIONS_DIR)
  } elseif ($env:VSCODE_PORTABLE) {
    $ExtensionRoots = @(Join-Path $env:VSCODE_PORTABLE "extensions")
  } else {
    $Candidates = @(
      (Join-Path $env:USERPROFILE ".vscode\extensions"),
      (Join-Path $env:USERPROFILE ".vscode-insiders\extensions"),
      (Join-Path $env:USERPROFILE ".vscode-oss\extensions"),
      (Join-Path $env:USERPROFILE ".cursor\extensions")
    )
    $ExtensionRoots = @($Candidates | Where-Object { Test-Path $_ })
  }

  foreach ($ExtensionsRoot in ($ExtensionRoots | Select-Object -Unique)) {
    $ExtensionDirs = @(
      (Join-Path $ExtensionsRoot "local.ferra-0.0.3"),
      (Join-Path $ExtensionsRoot "local.ferra-0.0.2"),
      (Join-Path $ExtensionsRoot "local.ferra-0.0.1"),
      (Join-Path $ExtensionsRoot "local.fe-0.0.3"),
      (Join-Path $ExtensionsRoot "local.fe-0.0.2"),
      (Join-Path $ExtensionsRoot "local.fe-0.0.1"),
      (Join-Path $ExtensionsRoot "local.efe-0.0.1"),
      (Join-Path $ExtensionsRoot "local.eferra-0.0.1")
    )
    foreach ($ExtensionDir in $ExtensionDirs) {
      if (
        (Test-Path (Join-Path $ExtensionDir "server\ferra_lsp.py")) -or
        (Test-Path (Join-Path $ExtensionDir "server\eferra_lsp.py"))
      ) {
        Remove-Item -Recurse -Force $ExtensionDir
      }
    }
  }
}
if (Test-Path $InstallDir) {
  Remove-Item -Recurse -Force $InstallDir
}

Write-Host "Ferra was removed. Open a new terminal to refresh PATH."
