# Removes the Canon LBP-810 printer(s), driver, driver package and (optionally) the certificate.
param([switch]$RemoveCertificate)
$ErrorActionPreference = 'Continue'
$driverName = 'Canon LBP-810'
Get-Printer | Where-Object DriverName -eq $driverName | ForEach-Object {
    Write-Host "Removing printer $($_.Name)"
    Remove-Printer -Name $_.Name
}
Restart-Service spooler -Force
Remove-PrinterDriver -Name $driverName -RemoveFromDriverStore -ErrorAction SilentlyContinue
# Remove any remaining staged copies of the package
& pnputil.exe /enum-drivers | Out-String -Stream | Select-String -Pattern 'Published Name|Опубликованное имя|Original Name|Исходное имя' | ForEach-Object { $_.Line } |
    ForEach-Object -Begin { $pub = $null } -Process {
        if ($_ -match '(oem\d+\.inf)') { $pub = $Matches[1] }
        elseif ($_ -match 'cnlb810\.inf' -and $pub) { & pnputil.exe /delete-driver $pub /uninstall /force; $pub = $null }
    }
if ($RemoveCertificate) {
    foreach ($store in 'Root', 'TrustedPublisher') {
        Get-ChildItem "Cert:\LocalMachine\$store" | Where-Object Subject -like '*relbp810*' | Remove-Item
    }
}
Write-Host 'Done.'
