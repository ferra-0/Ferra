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

function Normalize-TestOutput([string]$Text) {
  return (($Text -replace "`r`n", "`n") -replace "`r", "`n").Trim()
}

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
    $Actual = Normalize-TestOutput (
      & (Join-Path $BuildDir "efe.exe") ".\eferra\tests\$Name.efe" |
        Out-String
    )
    if ($LASTEXITCODE -ne 0) { throw "$Name exited with $LASTEXITCODE" }
    $Expected = Normalize-TestOutput (
      Get-Content ".\eferra\tests\$Name.out" -Raw
    )

    # Git may check text fixtures out with CRLF on Windows. The stream test
    # opens its fixture in binary mode, so derive the expected byte line from
    # the bytes that are actually present instead of assuming an LF checkout.
    if ($Name -eq "native_file_stream") {
      $FixtureBytes = [IO.File]::ReadAllBytes(
        (Join-Path $ProjectRoot "eferra\tests\stream_fixture.txt")
      )
      $ExpectedByteValues = @($FixtureBytes | ForEach-Object {
        ([double]$_).ToString(
          "F6",
          [Globalization.CultureInfo]::InvariantCulture
        )
      })
      $ExpectedLines = @($Expected -split "`n")
      $ExpectedLines[0] = "[" + ($ExpectedByteValues -join ", ") + "]"
      $Expected = $ExpectedLines -join "`n"
    }

    if ($Actual -ne $Expected) {
      Write-Host "--- expected: $Name ---"
      Write-Host $Expected
      Write-Host "--- actual: $Name ---"
      Write-Host $Actual
      throw "$Name output differs"
    }
  }
} finally {
  Pop-Location
}

# Exercise the wrapper and the packaged iron.efe, not only efe.exe itself.
$IronSmokeDir = Join-Path ([IO.Path]::GetTempPath()) (
  "ferra-iron-package-test-" + [Guid]::NewGuid().ToString("N")
)
New-Item -ItemType Directory $IronSmokeDir | Out-Null
Push-Location $IronSmokeDir
try {
  & (Join-Path $InstallDir "bin\iron.cmd") new
  if ($LASTEXITCODE -ne 0) { throw "Packaged Iron smoke test failed" }
  if (-not (Test-Path "ferra.json") -or -not (Test-Path "main.fe")) {
    throw "Packaged Iron did not create a project"
  }
} finally {
  Pop-Location
  if (Test-Path $IronSmokeDir) { Remove-Item -Recurse -Force $IronSmokeDir }
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
