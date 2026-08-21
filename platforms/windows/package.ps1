param(
  [string]$Configuration = "Release",
  [string]$BuildDir = "",
  [string]$InstallDir = "",
  [string]$ReleaseDir = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $BuildDir) { $BuildDir = Join-Path $ProjectRoot "build\windows" }
if (-not $InstallDir) { $InstallDir = Join-Path $ProjectRoot "dist\windows" }
if (-not $ReleaseDir) { $ReleaseDir = Join-Path $ProjectRoot "release" }

& (Join-Path $PSScriptRoot "build.ps1") `
  -Configuration $Configuration -BuildDir $BuildDir -InstallDir $InstallDir
if ($LASTEXITCODE -ne 0) { throw "Windows build failed with code $LASTEXITCODE" }

$EFerraTests = @(
  "native_timer",
  "native_json",
  "primitive_methods",
  "native_thread",
  "native_thread_many",
  "native_thread_autojoin",
  "native_buffer",
  "native_file_stream",
  "native_process"
)
Push-Location $ProjectRoot
try {
  foreach ($Name in $EFerraTests) {
    $Actual = (& (Join-Path $BuildDir "efe.exe") ".\eferra\tests\$Name.efe" | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw "$Name exited with $LASTEXITCODE" }
    $Expected = (Get-Content ".\eferra\tests\$Name.out" -Raw).Trim()
    if ($Actual -ne $Expected) { throw "$Name output differs" }
  }
} finally {
  Pop-Location
}

New-Item -ItemType Directory -Force $ReleaseDir | Out-Null
cpack --config (Join-Path $BuildDir "CPackConfig.cmake") -G ZIP -B $ReleaseDir
if ($LASTEXITCODE -ne 0) { throw "CPack failed with code $LASTEXITCODE" }

$Archive = Get-ChildItem $ReleaseDir -Filter "ferra-*-windows-*.zip" |
  Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $Archive) { throw "Windows ZIP was not created" }
cmake -E tar tf $Archive.FullName | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Cannot read generated ZIP" }

Write-Host "Ready to publish: $($Archive.FullName)"
Write-Host "Checksum: $($Archive.FullName).sha256"
