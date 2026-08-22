# Ferra 1.16: Windows, Linux, macOS and Android/Termux

Ferra supports Windows, Linux, macOS and Android through Termux.

## Build a publishable ZIP

One command builds every Ferra binary for the current operating system, runs
the tests, creates an installable ZIP and writes its SHA-256 checksum.

Linux or macOS:

```bash
./package.sh
```

Windows PowerShell:

```powershell
.\package.ps1
```

Ready-to-upload files are written to `release/`:

```text
ferra-1.16.0-linux-x86_64.zip
ferra-1.16.0-linux-x86_64.zip.sha256
```

The same command on macOS or Windows produces the corresponding native ZIP.
Native binaries must be built on their target OS; the release workflow builds
all three packages in parallel.

On Android, run the same command inside Termux. It automatically creates a
separate `ferra-1.16.0-android-<architecture>.zip` (normally `arm64`); Android
binaries are not compatible with desktop Linux binaries.

## Install a downloaded ZIP

Linux/macOS/Android-Termux:

```bash
unzip ferra-*.zip
cd ferra-*/
./install.sh
```

Windows: extract the ZIP, open PowerShell in the extracted directory, then:

```powershell
.\install.ps1
```

No administrator access is needed. The Unix installer uses
`~/.local/opt/ferra` and exposes commands through `~/.local/bin`. The Windows
installer uses `%LOCALAPPDATA%\Programs\Ferra\bin`. Both update the user PATH,
install the complete Ferra/eFerra VS Code extension and language servers,
perform smoke tests and support installing a newer ZIP over an older version.
The LSP installation is offline; its Node dependencies are already in the ZIP.
Python 3 is required when the editor starts either language server.

Set `FERRA_NO_LSP_INSTALL=1` before `install.sh`, or pass `-NoLspInstall` to
`install.ps1`, to skip the editor integration on a headless machine.

Uninstall with `~/.local/opt/ferra/uninstall.sh` on Unix or
`%LOCALAPPDATA%\Programs\Ferra\uninstall.ps1` on Windows. Set
`FERRA_INSTALL_DIR`/`FERRA_BIN_DIR` on Unix, or pass `-InstallDir` on Windows,
to choose custom locations.

Clang is still required on the user's machine when `iron` should turn LLVM IR
into a native executable; the Ferra compiler and eFerra VM themselves are in
the ZIP.

## Linux

Ubuntu/Debian:

```bash
sudo apt install clang cmake ninja-build libcurl4-openssl-dev python3
```

Build an unpacked development tree:

```bash
./platforms/linux/build.sh
```

Result: `dist/linux/`. Use `./package.sh` for the distributable ZIP.

## macOS

Install tools:

```bash
xcode-select --install
brew install cmake ninja curl python
```

Build an unpacked development tree:

```bash
./platforms/macos/build.sh
```

Result: `dist/macos/`. It builds natively for the current Mac architecture
(`arm64` or `x86_64`). Use `./package.sh` for the distributable ZIP.

## Android / Termux

Install the native Termux dependencies and build on the Android device:

```bash
pkg install clang cmake ninja libcurl python
./package.sh
```

Result: `dist/android/` and an installable
`release/ferra-1.16.0-android-<architecture>.zip`. The ZIP is for Termux on
Android and must not be published as the desktop Linux package.

## Windows

Install tools with vcpkg:

```powershell
vcpkg install curl:x64-windows
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
```

Run in powershell:

```powershell
.\platforms\windows\build.ps1
```

Result: `dist\windows\`. Use `.\package.ps1` for the distributable ZIP.

## Package usage

Add directory `bin` to PATH. Then:

```bash
ferra program.fe -o program.ll
efe script.efe
iron new
iron
```

## VS Code and LSP

The downloaded ZIP installers configure VS Code automatically. To reinstall
the editor integration manually from a development checkout, use:

Linux/macOS:

```bash
./lang.sh
```

Windows PowerShell:

```powershell
.\platforms\windows\install-vscode.ps1
```

Both commands install exactly the same Ferra/eFerra grammar, icons, offline
language client and Python language servers. Restart VS Code after installation.
The Ferra uninstaller removes this local extension unless
`FERRA_KEEP_LSP=1` or `-KeepLsp` is used.

## Checking

Linux/macOS after build:

```bash
ctest --test-dir build/linux --output-on-failure
# or build/macos on macOS
```
