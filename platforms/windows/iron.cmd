@echo off
setlocal
set "BIN_DIR=%~dp0"
set "FERRA_ROOT=%BIN_DIR%..\share\ferra"
set "FERRA_PATH=%FERRA_ROOT%"
"%BIN_DIR%efe.exe" "%FERRA_ROOT%\ferralang\iron.efe" %*
exit /b %errorlevel%
