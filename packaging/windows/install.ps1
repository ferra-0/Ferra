param(
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\Ferra"),
  [switch]$NoPathUpdate,
  [switch]$NoLspInstall,
  [string]$ExtensionsRoot = ""
)

$ErrorActionPreference = "Stop"
$PackageDir = $PSScriptRoot
$BinDir = Join-Path $InstallDir "bin"

foreach ($Required in @(
  "bin\ferra.exe",
  "bin\efe.exe",
  "bin\iron.cmd",
  "share\ferra\lang.sh",
  "share\ferra\packaging\vscode\make-vsix.py",
  "share\ferra\icons\ferra-dark.png",
  "share\ferra\icons\ferra-light.png",
  "share\ferra\ferralang\lsp\ferra_lsp.py",
  "share\ferra\eferra\lsp\eferra_lsp.py",
  "share\ferra\ferralang\lsp\client\node_modules\vscode-languageclient",
  "share\ferra\platforms\windows\install-vscode.ps1",
  "lib\ferra",
  "uninstall.ps1"
)) {
  if (-not (Test-Path (Join-Path $PackageDir $Required))) {
    throw "Package is incomplete: missing $Required"
  }
}

$InstallParent = Split-Path -Parent $InstallDir
$StagingDir = Join-Path $InstallParent (".ferra-install-new-" + [Guid]::NewGuid().ToString("N"))
$BackupDir = Join-Path $InstallParent (".ferra-install-old-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $InstallParent, $StagingDir | Out-Null

try {
  Copy-Item -Recurse (Join-Path $PackageDir "bin") (Join-Path $StagingDir "bin")
  Copy-Item -Recurse (Join-Path $PackageDir "lib") (Join-Path $StagingDir "lib")
  Copy-Item -Recurse (Join-Path $PackageDir "share") (Join-Path $StagingDir "share")
  Copy-Item (Join-Path $PackageDir "uninstall.ps1") $StagingDir
  if (Test-Path (Join-Path $PackageDir "README.md")) {
    Copy-Item (Join-Path $PackageDir "README.md") $StagingDir
  }

  if (Test-Path $InstallDir) { Move-Item $InstallDir $BackupDir }
  Move-Item $StagingDir $InstallDir
  if (Test-Path $BackupDir) { Remove-Item -Recurse -Force $BackupDir }
} catch {
  if (Test-Path $StagingDir) { Remove-Item -Recurse -Force $StagingDir }
  if ((-not (Test-Path $InstallDir)) -and (Test-Path $BackupDir)) {
    Move-Item $BackupDir $InstallDir
  }
  throw
}

if (-not $NoPathUpdate) {
  $UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
  $Entries = @($UserPath -split ";" | Where-Object { $_ })
  $AlreadyPresent = $Entries | Where-Object {
    $_.TrimEnd("\") -ieq $BinDir.TrimEnd("\")
  }
  if (-not $AlreadyPresent) {
    $UpdatedPath = (@($BinDir) + $Entries) -join ";"
    [Environment]::SetEnvironmentVariable(
      "Path",
      $UpdatedPath,
      "User"
    )
  }
  if (-not (($env:Path -split ";") | Where-Object { $_.TrimEnd("\") -ieq $BinDir.TrimEnd("\") })) {
    $env:Path = "$BinDir;$env:Path"
  }
}

$SmokeDir = Join-Path ([IO.Path]::GetTempPath()) ("ferra-install-test-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $SmokeDir | Out-Null
try {
  $FerraSource = Join-Path $SmokeDir "package.fe"
  $EFerraSource = Join-Path $SmokeDir "package.efe"
  [IO.File]::WriteAllText($FerraSource, "take `"fe/math.fe`"`nfn main(): i64 { ret 0 }`n")
  [IO.File]::WriteAllText($EFerraSource, "log(`"package-ok`")`n")
  & (Join-Path $BinDir "ferra.exe") $FerraSource -o (Join-Path $SmokeDir "package.ll") | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "Ferra smoke test failed" }
  $EFerraOutput = (& (Join-Path $BinDir "efe.exe") $EFerraSource | Out-String).Trim()
  if ($LASTEXITCODE -ne 0 -or $EFerraOutput -ne "package-ok") {
    throw "eFerra smoke test failed"
  }

  $IronProjectDir = Join-Path $SmokeDir "iron-project"
  New-Item -ItemType Directory $IronProjectDir | Out-Null
  Push-Location $IronProjectDir
  try {
    & (Join-Path $BinDir "iron.cmd") new
    if ($LASTEXITCODE -ne 0) { throw "Iron smoke test failed" }
    if (-not (Test-Path "ferra.json") -or -not (Test-Path "main.fe")) {
      throw "Iron did not create a project"
    }
  } finally {
    Pop-Location
  }
} finally {
  if (Test-Path $SmokeDir) { Remove-Item -Recurse -Force $SmokeDir }
}

$LspInstalled = $false
if (-not $NoLspInstall) {
  $LspInstaller = Join-Path $InstallDir "share\ferra\platforms\windows\install-vscode.ps1"
  if ($ExtensionsRoot) {
    & $LspInstaller -ExtensionsRoot $ExtensionsRoot
  } else {
    & $LspInstaller
  }
  $LspInstalled = $true
}

Write-Host "Ferra installed successfully."
Write-Host "  Files: $InstallDir"
Write-Host "  PATH entry: $BinDir"
Write-Host "Open a new terminal, then use: ferra, efe, iron"
if (-not (Get-Command clang -ErrorAction SilentlyContinue)) {
  Write-Warning "Install LLVM/Clang to let Iron turn generated LLVM IR into executables."
}
if ($LspInstalled) {
  Write-Host "  LSP: Ferra/eFerra VS Code support installed"
  if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Warning "Install Python 3 to run the Ferra language servers."
  }
}
Write-Host "Uninstall with: $InstallDir\uninstall.ps1"
