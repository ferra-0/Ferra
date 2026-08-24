param(
  [string]$ExtensionsRoot = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$ExtensionVersion = "0.0.4"
$ExtensionId = "local.ferra"
$script:FirstExtensionDir = $null

function Read-BashHereDoc([string]$Text, [string]$Marker) {
  $Start = $Text.IndexOf($Marker, [System.StringComparison]::Ordinal)
  if ($Start -lt 0) { throw "Cannot find template marker: $Marker" }
  $Start = $Text.IndexOf("`n", $Start) + 1
  $End = $Text.IndexOf("`nEOF", $Start, [System.StringComparison]::Ordinal)
  if ($End -lt 0) { throw "Cannot find template end for: $Marker" }
  return $Text.Substring($Start, $End - $Start).Replace("`r", "")
}

function Invoke-EditorCli([string]$Editor, [string[]]$Arguments) {
  # Windows PowerShell wraps every native stderr line in NativeCommandError.
  # VS Code can print harmless Node.js deprecation warnings there while still
  # returning exit code 0, so only the actual native exit code decides success.
  $PreviousErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  $HasNativeErrorPreference = Test-Path Variable:PSNativeCommandUseErrorActionPreference
  if ($HasNativeErrorPreference) {
    $PreviousNativeErrorPreference = $PSNativeCommandUseErrorActionPreference
    $PSNativeCommandUseErrorActionPreference = $false
  }
  try {
    $OutputLines = @(
      & $Editor @Arguments 2>&1 | ForEach-Object { $_.ToString() }
    )
    $ExitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $PreviousErrorActionPreference
    if ($HasNativeErrorPreference) {
      $PSNativeCommandUseErrorActionPreference = $PreviousNativeErrorPreference
    }
  }
  return [PSCustomObject]@{
    ExitCode = $ExitCode
    Output = ($OutputLines -join [Environment]::NewLine).Trim()
  }
}

$InstallerSource = [System.IO.File]::ReadAllText((Join-Path $ProjectRoot "lang.sh"))
$PackageJson = Read-BashHereDoc $InstallerSource 'cat > "$EXT_DIR/package.json" <<''EOF'''
$LanguageConfig = Read-BashHereDoc $InstallerSource 'cat > "$EXT_DIR/language-configuration.json" <<''EOF'''
$FerraGrammar = Read-BashHereDoc $InstallerSource 'cat > "$SYNTAX_DIR/ferra.tmLanguage.json" <<''EOF'''
$PackageJson = $PackageJson.Replace('"default": "python3"', '"default": "python"')

function Install-FerraVscodeExtension([string]$TargetRoot) {
  $ExtensionDir = Join-Path $TargetRoot "$ExtensionId-$ExtensionVersion"
  $LegacyExtensionDirs = @(
    (Join-Path $TargetRoot "local.ferra-0.0.3"),
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

  if (-not $script:FirstExtensionDir) {
    $script:FirstExtensionDir = $ExtensionDir
  }
  Write-Host "Ferra extension files prepared at: $ExtensionDir"
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

$VsixWorkDir = Join-Path ([IO.Path]::GetTempPath()) (
  "ferra-vsix-install-" + [Guid]::NewGuid().ToString("N")
)
$VsixStage = Join-Path $VsixWorkDir "package"
$VsixPath = Join-Path $VsixWorkDir "ferra-$ExtensionVersion.vsix"
New-Item -ItemType Directory -Force $VsixStage | Out-Null
try {
  Copy-Item -Recurse -Force $script:FirstExtensionDir (Join-Path $VsixStage "extension")
  $Manifest = Get-Content (Join-Path $script:FirstExtensionDir "package.json") -Raw |
    ConvertFrom-Json
  $XmlName = [Security.SecurityElement]::Escape([string]$Manifest.name)
  $XmlPublisher = [Security.SecurityElement]::Escape([string]$Manifest.publisher)
  $XmlVersion = [Security.SecurityElement]::Escape([string]$Manifest.version)
  $XmlDisplayName = [Security.SecurityElement]::Escape([string]$Manifest.displayName)
  $XmlEngine = [Security.SecurityElement]::Escape([string]$Manifest.engines.vscode)
  $ContentTypes = @'
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="json" ContentType="application/json" />
  <Default Extension="js" ContentType="application/javascript" />
  <Default Extension="py" ContentType="text/x-python" />
  <Default Extension="png" ContentType="image/png" />
  <Default Extension="md" ContentType="text/markdown" />
  <Default Extension="txt" ContentType="text/plain" />
  <Default Extension="vsixmanifest" ContentType="text/xml" />
</Types>
'@
  $VsixManifest = @"
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="$XmlName" Version="$XmlVersion" Publisher="$XmlPublisher" />
    <DisplayName>$XmlDisplayName</DisplayName>
    <Description xml:space="preserve">Ferra and eFerra language support</Description>
    <Categories>Programming Languages</Categories>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="$XmlEngine" />
      <Property Id="Microsoft.VisualStudio.Services.Content.Pricing" Value="Free" />
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
  </Assets>
</PackageManifest>
"@
  [IO.File]::WriteAllText((Join-Path $VsixStage "[Content_Types].xml"), $ContentTypes)
  [IO.File]::WriteAllText((Join-Path $VsixStage "extension.vsixmanifest"), $VsixManifest)
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  [IO.Compression.ZipFile]::CreateFromDirectory(
    $VsixStage,
    $VsixPath,
    [IO.Compression.CompressionLevel]::Optimal,
    $false
  )

  $EditorCandidates = @()
  foreach ($CommandName in @("code", "code-insiders", "code-oss", "codium", "cursor")) {
    $Command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($Command) { $EditorCandidates += $Command.Source }
  }
  $ProgramFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
  $EditorCandidates += @(
    (Join-Path $env:LOCALAPPDATA "Programs\Microsoft VS Code\bin\code.cmd"),
    (Join-Path $env:LOCALAPPDATA "Programs\Microsoft VS Code Insiders\bin\code-insiders.cmd"),
    (Join-Path $env:LOCALAPPDATA "Programs\Cursor\resources\app\bin\cursor.cmd"),
    (Join-Path $env:LOCALAPPDATA "Programs\VSCodium\bin\codium.cmd"),
    (Join-Path $env:ProgramFiles "Microsoft VS Code\bin\code.cmd"),
    (Join-Path $env:ProgramFiles "VSCodium\bin\codium.cmd")
  )
  if ($ProgramFilesX86) {
    $EditorCandidates += Join-Path $ProgramFilesX86 "Microsoft VS Code\bin\code.cmd"
  }
  $EditorCandidates = @($EditorCandidates | Where-Object {
    $_ -and (Test-Path $_)
  } | Select-Object -Unique)

  $EditorArgs = @()
  if ($ExtensionsRoot) {
    $EditorArgs = @("--extensions-dir", $ExtensionsRoot)
  } elseif ($env:VSCODE_EXTENSIONS) {
    $EditorArgs = @("--extensions-dir", $env:VSCODE_EXTENSIONS)
  } elseif ($env:VSCODE_EXTENSIONS_DIR) {
    $EditorArgs = @("--extensions-dir", $env:VSCODE_EXTENSIONS_DIR)
  } elseif ($env:VSCODE_PORTABLE) {
    $EditorArgs = @("--extensions-dir", (Join-Path $env:VSCODE_PORTABLE "extensions"))
  }

  $InstalledEditors = 0
  foreach ($Editor in $EditorCandidates) {
    Write-Host "Registering Ferra VSIX with: $Editor"
    $InstallArguments = @($EditorArgs) + @(
      "--install-extension", $VsixPath, "--force"
    )
    $InstallResult = Invoke-EditorCli $Editor $InstallArguments
    if ($InstallResult.ExitCode -ne 0) {
      Write-Warning "VSIX installation failed in $Editor (exit $($InstallResult.ExitCode)): $($InstallResult.Output)"
      continue
    }
    $ListArguments = @($EditorArgs) + @("--list-extensions", "--show-versions")
    $ListResult = Invoke-EditorCli $Editor $ListArguments
    $ExtensionList = $ListResult.Output
    $ExpectedExtension = "$ExtensionId@$ExtensionVersion"
    $ExtensionLines = @($ExtensionList -split "`r?`n" | ForEach-Object { $_.Trim() })
    if ($ListResult.ExitCode -eq 0 -and $ExtensionLines -contains $ExpectedExtension) {
      $InstalledEditors++
    } else {
      Write-Warning "$Editor did not report $ExtensionId@$ExtensionVersion after installation. $ExtensionList"
    }
  }

  if ($EditorCandidates.Count -gt 0 -and $InstalledEditors -eq 0) {
    throw "An editor CLI was found, but none registered the Ferra extension. VS Code 1.91 or newer is required."
  }
  if ($EditorCandidates.Count -eq 0) {
    Write-Warning "No VS Code-compatible CLI was found; installed the extension directory directly."
  }
} finally {
  if (Test-Path $VsixWorkDir) { Remove-Item -Recurse -Force $VsixWorkDir }
}

Write-Host "Ferra/eFerra syntax, hover, diagnostics and inlay hints are installed."
Write-Host "Restart the editor completely."
