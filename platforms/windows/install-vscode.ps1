param(
  [string]$ExtensionsRoot = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $ExtensionsRoot) {
  if ($env:VSCODE_EXTENSIONS_DIR) {
    $ExtensionsRoot = $env:VSCODE_EXTENSIONS_DIR
  } else {
    $ExtensionsRoot = Join-Path $env:USERPROFILE ".vscode\extensions"
  }
}

$ExtensionDir = Join-Path $ExtensionsRoot "local.fe-0.0.1"
$LegacyEFerraExtensionDir = Join-Path $ExtensionsRoot "local.efe-0.0.1"
$SyntaxDir = Join-Path $ExtensionDir "syntaxes"
$IconDir = Join-Path $ExtensionDir "icons"
$ServerDir = Join-Path $ExtensionDir "server"

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

if (Test-Path $ExtensionDir) { Remove-Item -Recurse -Force $ExtensionDir }
if (Test-Path $LegacyEFerraExtensionDir) {
  Remove-Item -Recurse -Force $LegacyEFerraExtensionDir
}
New-Item -ItemType Directory -Force $SyntaxDir, $IconDir, $ServerDir | Out-Null

Copy-Item (Join-Path $ProjectRoot "icons\ferra-dark.png") $IconDir
Copy-Item (Join-Path $ProjectRoot "icons\ferra-light.png") $IconDir
Copy-Item (Join-Path $ProjectRoot "ferralang\lsp\ferra_lsp.py") $ServerDir
Copy-Item (Join-Path $ProjectRoot "eferra\lsp\eferra_lsp.py") $ServerDir
Copy-Item (Join-Path $ProjectRoot "eferra\lsp\eferra.tmLanguage.json") $SyntaxDir
Copy-Item (Join-Path $ProjectRoot "ferralang\lsp\client\extension.js") $ExtensionDir

[System.IO.File]::WriteAllText((Join-Path $ExtensionDir "package.json"), $PackageJson)
[System.IO.File]::WriteAllText((Join-Path $ExtensionDir "language-configuration.json"), $LanguageConfig)
[System.IO.File]::WriteAllText((Join-Path $SyntaxDir "ferra.tmLanguage.json"), $FerraGrammar)

$ClientModules = Join-Path $ProjectRoot "ferralang\lsp\client\node_modules"
if (Test-Path (Join-Path $ClientModules "vscode-languageclient")) {
  Copy-Item -Recurse $ClientModules (Join-Path $ExtensionDir "node_modules")
} else {
  if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
    throw "npm is required to install vscode-languageclient"
  }
  npm install --omit=dev --ignore-scripts --prefix $ExtensionDir
  if ($LASTEXITCODE -ne 0) { throw "npm install failed with code $LASTEXITCODE" }
}

Write-Host "Ferra VS Code support installed to: $ExtensionDir"
Write-Host "Restart VS Code completely."
