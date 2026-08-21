@echo off
setlocal
set "BIN_DIR=%~dp0"
if not defined FERRA_PATH set "FERRA_PATH=%BIN_DIR%..\share\ferra"
"%BIN_DIR%efe.exe" "%FERRA_PATH%\ferralang\iron.efe" %*
exit /b %errorlevel%
