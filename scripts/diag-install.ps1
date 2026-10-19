# Diagnostics for driver/queue installation problems (run as Administrator).
$ErrorActionPreference = 'Continue'
$driverName = 'Canon LBP-810'
& wevtutil.exe sl Microsoft-Windows-PrintService/Operational /e:true
$start = Get-Date
"--- Add-PrinterDriver"
try { Add-PrinterDriver -Name $driverName -ErrorAction Stop; "ok" } catch { "FAILED: $($_.Exception.Message) HRESULT=0x{0:X8}" -f $_.Exception.HResult; $_.Exception.InnerException }
"--- Get-PrinterDriver"
Get-PrinterDriver -Name $driverName -ErrorAction SilentlyContinue | Format-List Name, MajorVersion, ConfigFile, DataFile, DependentFiles, MonitorName, InfPath | Out-String -Width 300
"--- Add-Printer"
$port = Get-PrinterPort | Where-Object { $_.Description -like '*LBP-810*' } | Select-Object -First 1
if (-not $port) { $port = Get-PrinterPort -Name 'USB001' -ErrorAction SilentlyContinue }
"port: $($port.Name)"
if (-not (Get-Printer -Name $driverName -ErrorAction SilentlyContinue)) {
    try { Add-Printer -Name $driverName -DriverName $driverName -PortName $port.Name -ErrorAction Stop; "ok" } catch { "FAILED: $($_.Exception.Message) HRESULT=0x{0:X8}" -f $_.Exception.HResult }
}
Get-Printer | Format-Table Name, DriverName, PortName, PrinterStatus -AutoSize | Out-String -Width 300
"--- Monitors"
Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors' | ForEach-Object { "$($_.PSChildName): $((Get-ItemProperty $_.PSPath).Driver)" }
"--- PrintService events"
Start-Sleep -Seconds 2
foreach ($log in 'Microsoft-Windows-PrintService/Admin', 'Microsoft-Windows-PrintService/Operational') {
    Get-WinEvent -LogName $log -ErrorAction SilentlyContinue | Where-Object TimeCreated -ge $start.AddSeconds(-5) |
        Sort-Object TimeCreated | Format-List TimeCreated, Id, LevelDisplayName, Message | Out-String -Width 300
}
