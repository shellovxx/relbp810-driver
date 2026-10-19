# Installs the Canon LBP-810 driver package (run as Administrator).
#   1. trusts the package's self-signed certificate (LocalMachine Root + TrustedPublisher)
#   2. stages and installs the driver package (pnputil)
#   3. creates the "Canon LBP-810" printer on the printer's USB port if PnP did not
# -EnableDebug turns on logging to %ProgramData%\LBP810x.
param(
    [string]$Package = (Join-Path (Split-Path -Parent $PSScriptRoot) 'out\package'),
    [string]$PrinterName = 'Canon LBP-810',
    [switch]$EnableDebug
)
$ErrorActionPreference = 'Stop'
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script as Administrator'
}
$inf = Join-Path $Package 'CNLB810.INF'
$cer = Join-Path $Package 'cnlb810.cer'
$driverName = 'Canon LBP-810'

Write-Host '[1/4] Trusting the driver signing certificate'
Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null

Write-Host '[2/4] Preparing %ProgramData%\LBP810x'
$data = Join-Path $env:ProgramData 'LBP810x'
New-Item -ItemType Directory -Force -Path $data | Out-Null
& icacls $data /grant '*S-1-5-32-545:(OI)(CI)M' /grant '*S-1-5-19:(OI)(CI)M' | Out-Null
foreach ($f in 'debug.on', 'dumppages.on') {
    $flag = Join-Path $data $f
    if ($EnableDebug) { Set-Content -Path $flag -Value '' } else { Remove-Item $flag -ErrorAction SilentlyContinue }
}

Write-Host '[3/4] Installing the driver package'
& pnputil.exe /add-driver $inf /install
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 3010) { throw "pnputil failed ($LASTEXITCODE)" }
if (-not (Get-PrinterDriver -Name $driverName -ErrorAction SilentlyContinue)) {
    Add-PrinterDriver -Name $driverName
}

Write-Host '[4/4] Creating the printer queue'
Start-Sleep -Seconds 3
$existing = Get-Printer | Where-Object DriverName -eq $driverName
if (-not $existing) {
    $port = Get-PrinterPort | Where-Object { $_.Description -like '*LBP-810*' } | Select-Object -First 1
    if (-not $port) { $port = Get-PrinterPort -Name 'USB001' -ErrorAction SilentlyContinue }
    if (-not $port) { throw 'USB port of the printer not found; connect the printer and retry' }
    Add-Printer -Name $PrinterName -DriverName $driverName -PortName $port.Name
}
Get-Printer | Where-Object DriverName -eq $driverName | Format-Table Name, PortName, DriverName -AutoSize
Write-Host 'Done.'
