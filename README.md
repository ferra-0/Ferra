# Ferra 1.16: Windows, Linux and macOS

Ferra supports Windows, Linux and macOS.

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

## Install a downloaded ZIP

Linux/macOS:

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
perform Ferra/eFerra smoke tests and support installing a newer ZIP over an
older version.

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

Linux/macOS:

```bash
./lang.sh
```

Windows PowerShell:

```powershell
.\platforms\windows\install-vscode.ps1
```

Both installers install exactly the same Ferra/eFerra grammar, icons and Python
language servers.

## Checking

Linux/macOS after build:

```bash
ctest --test-dir build/linux --output-on-failure
# or build/macos on macOS
```
