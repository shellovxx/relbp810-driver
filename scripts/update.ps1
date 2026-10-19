# Development: updates an installed driver in place from out\package (x64, run as Administrator).
$ErrorActionPreference = 'Continue'
$pkg = Join-Path (Split-Path -Parent $PSScriptRoot) 'out\package'
& pnputil.exe /add-driver "$pkg\CNLB810.INF" /install
Add-PrinterDriver -Name 'Canon LBP-810' -ErrorAction SilentlyContinue
# The spooler keeps the language monitor loaded, so the copy in System32 is not replaced by the
# driver update; swap it while the spooler is stopped.
Stop-Service spooler -Force
Start-Sleep -Seconds 1
Copy-Item "$pkg\amd64\CNLB810L.DLL" C:\Windows\System32\CNLB810L.DLL -Force
Start-Service spooler
Start-Sleep -Seconds 2
Get-PrinterDriver -Name 'Canon LBP-810' | Format-List Name, DriverVersion, InfPath | Out-String
Get-Item C:\Windows\System32\spool\drivers\x64\3\CNLB810R.DLL | Select-Object Name, LastWriteTime, @{n='Ver';e={$_.VersionInfo.FileVersion}} | Format-List | Out-String
Get-Item C:\Windows\System32\CNLB810L.DLL | Select-Object Name, LastWriteTime, @{n='Ver';e={$_.VersionInfo.FileVersion}} | Format-List | Out-String
