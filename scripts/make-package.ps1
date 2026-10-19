# Assembles out\package (INF + amd64 + x86), builds the security catalog and signs it.
# A self-signed code signing certificate is created in Cert:\CurrentUser\My on first use
# and exported to out\package\cnlb810.cer (install.ps1 trusts it on the target machine).
param(
    [string]$CertSubject = "CN=relbp810 LBP-810 driver (self-signed)"
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pkg = Join-Path $root 'out\package'
# makecat/signtool from the newest installed Windows 10/11 SDK.
$kit = Get-ChildItem (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin') -Directory -Filter '10.*' -ErrorAction SilentlyContinue |
    Sort-Object { [version]$_.Name } -Descending | ForEach-Object { Join-Path $_.FullName 'x64' } |
    Where-Object { Test-Path (Join-Path $_ 'makecat.exe') } | Select-Object -First 1
if (-not $kit) { throw 'Windows SDK (makecat.exe, signtool.exe) not found; install the Windows 10/11 SDK' }
$makecat = Join-Path $kit 'makecat.exe'
$signtool = Join-Path $kit 'signtool.exe'

foreach ($a in 'amd64', 'x86') {
    if (-not (Test-Path (Join-Path $pkg "$a\CNLB810R.DLL"))) { throw "Build $a first (build.cmd)" }
}
Copy-Item (Join-Path $root 'src\driver\inf\CNLB810.INF') $pkg -Force

# Catalog definition: every file referenced by the INF, for both architectures.
$cdf = Join-Path $root 'build\cnlb810.cdf'
$lines = @(
    '[CatalogHeader]',
    'Name=cnlb810.cat',
    "ResultDir=$pkg",
    'PublicVersion=0x0000001',
    'EncodingType=0x00010001',
    'CATATTR1=0x10010001:OSAttr:2:6.1,2:6.2,2:6.3,2:10.0',
    '[CatalogFiles]',
    "<hash>CNLB810.INF=$pkg\CNLB810.INF"
)
foreach ($a in 'amd64', 'x86') {
    Get-ChildItem (Join-Path $pkg $a) -File | ForEach-Object {
        $lines += "<hash>$a`_$($_.Name)=$($_.FullName)"
        $lines += "<hash>$a`_$($_.Name)ATTR1=0x10010001:File:$($_.Name)"
    }
}
Set-Content -Path $cdf -Value $lines -Encoding ASCII
& $makecat -v $cdf | Out-Null
if ($LASTEXITCODE -ne 0) { throw "makecat failed ($LASTEXITCODE)" }

$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $CertSubject -and $_.NotAfter -gt (Get-Date) } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $CertSubject -CertStoreLocation Cert:\CurrentUser\My `
        -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(15)
    Write-Host "Created signing certificate $($cert.Thumbprint)"
}
Export-Certificate -Cert $cert -FilePath (Join-Path $pkg 'cnlb810.cer') -Type CERT | Out-Null

$toSign = @((Join-Path $pkg 'cnlb810.cat'))
foreach ($a in 'amd64', 'x86') { $toSign += Get-ChildItem (Join-Path $pkg $a) -Filter *.DLL | ForEach-Object FullName }
& $signtool sign /q /fd sha256 /sha1 $cert.Thumbprint $toSign
if ($LASTEXITCODE -ne 0) { throw "signtool failed ($LASTEXITCODE)" }
# The DLL signatures changed the file hashes: rebuild and re-sign the catalog.
& $makecat -v $cdf | Out-Null
if ($LASTEXITCODE -ne 0) { throw "makecat failed ($LASTEXITCODE)" }
& $signtool sign /q /fd sha256 /sha1 $cert.Thumbprint (Join-Path $pkg 'cnlb810.cat')
if ($LASTEXITCODE -ne 0) { throw "signtool failed ($LASTEXITCODE)" }
Write-Host "Package ready: $pkg (signed by $($cert.Thumbprint))"
