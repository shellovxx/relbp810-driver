@echo off
rem Builds the distributable MSI: binaries (build.cmd), the signed driver package
rem (scripts\make-package.ps1) and out\LBP-810-driver-<version>.msi (scripts\make-msi.py).
rem The MSI installs on Windows 7 SP1 .. 11, x86 and x64, without PowerShell; the other scripts
rem in scripts\ are for development.
setlocal
set "ROOT=%~dp0"
call "%ROOT%build.cmd" || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\make-package.ps1" || exit /b 1
python "%ROOT%scripts\make-msi.py" || exit /b 1
