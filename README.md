# Ferra 1.16: Windows, Linux and macOS

Ferra support wor Windows, Linux and macOS

## Linux

Ubuntu/Debian:

```bash
sudo apt install clang cmake ninja-build libcurl4-openssl-dev python3
```

Build:

```bash
./platforms/linux/build.sh
```

Result: `dist/linux/`.

## macOS

Install tools:

```bash
xcode-select --install
brew install cmake ninja curl python
```

Build:

```bash
./platforms/macos/build.sh
```

Result: `dist/macos/`. Builds natively for current architecture
Mac (`arm64` or `x86_64`).

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

Result: `dist\windows\`.

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

Both of installers install exactly the same Ferra/eFerra grammar, icons and Python
language servers.

## Checking

Linux/macOS after build:

```bash
ctest --test-dir build/linux --output-on-failure
# or build/macos on macOS
```
