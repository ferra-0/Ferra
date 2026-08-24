param(
  [string]$ExtensionsRoot = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Read-BashHereDoc([string]$Text, [string]$Marker) {
  $Start = $Text.IndexOf($Marker, [System.StringComparison]::Ordinal)
  if ($Start -lt 0) { throw "Cannot find template marker: $Marker" }
  $Start = $Text.IndexOf("`n", $Start) + 1
  $End = $Text.IndexOf("`nEOF", $Start, [System.StringComparison]::Ordinal)
  if ($End -lt 0) { throw "Cannot find template end for: $Marker" }
  return $Text.Substring($Start, $End - $Start).Replace("`r", "")
}

$InstallerSource = [System.IO.File]::ReadAllText((Join-Path $ProjectRoot "lang.sh"))
$PackageJson = Read-BashHereDoc $InstallerSource 'cat > "$EXT_DIR/package.json" <<''EOF'''
$LanguageConfig = Read-BashHereDoc $InstallerSource 'cat > "$EXT_DIR/language-configuration.json" <<''EOF'''
$FerraGrammar = Read-BashHereDoc $InstallerSource 'cat > "$SYNTAX_DIR/ferra.tmLanguage.json" <<''EOF'''
$PackageJson = $PackageJson.Replace('"default": "python3"', '"default": "python"')

function Install-FerraVscodeExtension([string]$TargetRoot) {
  $ExtensionDir = Join-Path $TargetRoot "local.ferra-0.0.3"
  $LegacyExtensionDirs = @(
    (Join-Path $TargetRoot "local.ferra-0.0.2"),
    (Join-Path $TargetRoot "local.ferra-0.0.1"),
    (Join-Path $TargetRoot "local.fe-0.0.2"),
    (Join-Path $TargetRoot "local.fe-0.0.3"),
    (Join-Path $TargetRoot "local.fe-0.0.1"),
    (Join-Path $TargetRoot "local.efe-0.0.1"),
    (Join-Path $TargetRoot "local.eferra-0.0.1")
  )
  $SyntaxDir = Join-Path $ExtensionDir "syntaxes"
  $IconDir = Join-Path $ExtensionDir "icons"
  $ServerDir = Join-Path $ExtensionDir "server"

  if (Test-Path $ExtensionDir) { Remove-Item -Recurse -Force $ExtensionDir }
  foreach ($LegacyDir in $LegacyExtensionDirs) {
    if (Test-Path $LegacyDir) { Remove-Item -Recurse -Force $LegacyDir }
  }
  New-Item -ItemType Directory -Force $SyntaxDir, $IconDir, $ServerDir | Out-Null

  Copy-Item (Join-Path $ProjectRoot "icons\ferra-dark.png") $IconDir
  Copy-Item (Join-Path $ProjectRoot "icons\ferra-light.png") $IconDir
  Copy-Item (Join-Path $ProjectRoot "ferralang\lsp\ferra_lsp.py") $ServerDir
  Copy-Item (Join-Path $ProjectRoot "eferra\lsp\eferra_lsp.py") $ServerDir
  Copy-Item (Join-Path $ProjectRoot "eferra\lsp\eferra.tmLanguage.json") $SyntaxDir
  Copy-Item (Join-Path $ProjectRoot "ferralang\lsp\client\extension.js") $ExtensionDir
  [System.IO.File]::WriteAllText((Join-Path $ServerDir "ferra-root.txt"), $ProjectRoot)

  [System.IO.File]::WriteAllText((Join-Path $ExtensionDir "package.json"), $PackageJson)
  [System.IO.File]::WriteAllText((Join-Path $ExtensionDir "language-configuration.json"), $LanguageConfig)
  [System.IO.File]::WriteAllText((Join-Path $SyntaxDir "ferra.tmLanguage.json"), $FerraGrammar)
  $ClientModules = Join-Path $ProjectRoot "ferralang\lsp\client\node_modules"
  if (-not (Test-Path (Join-Path $ClientModules "vscode-languageclient"))) {
    # In a source checkout the offline client runtime is tracked with the
    # Ferra tests; packaged releases put the same files beside extension.js.
    $ClientModules = Join-Path $ProjectRoot "ferralang\tests\node_modules"
  }
  if (Test-Path (Join-Path $ClientModules "vscode-languageclient")) {
    Copy-Item -Recurse -Force $ClientModules (Join-Path $ExtensionDir "node_modules")
  } else {
    if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
      throw "npm is required to install vscode-languageclient"
    }
    npm install --omit=dev --ignore-scripts --prefix $ExtensionDir
    if ($LASTEXITCODE -ne 0) { throw "npm install failed with code $LASTEXITCODE" }
  }

  if (
    -not (Get-Command python -ErrorAction SilentlyContinue) -and
    -not (Get-Command py -ErrorAction SilentlyContinue)
  ) {
    Write-Warning "Python 3 was not found. Syntax highlighting will work, but configure ferra.lsp.pythonPath for hover and inlay hints."
  }

  Write-Host "Ferra VS Code support installed to: $ExtensionDir"
}

if ($ExtensionsRoot) {
  $ExtensionRoots = @($ExtensionsRoot)
} elseif ($env:VSCODE_EXTENSIONS) {
  # This is the environment variable honored by the VS Code CLI.
  $ExtensionRoots = @($env:VSCODE_EXTENSIONS)
} elseif ($env:VSCODE_EXTENSIONS_DIR) {
  # Kept for backwards compatibility with the Unix installer.
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
  if ($ExtensionRoots.Count -eq 0) {
    $ExtensionRoots = @($Candidates[0])
  }
}

foreach ($Root in ($ExtensionRoots | Select-Object -Unique)) {
  Install-FerraVscodeExtension $Root
}
Write-Host "Restart VS Code completely."
