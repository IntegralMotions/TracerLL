param(
    [Parameter(Mandatory)]
    [ValidateSet('Install', 'Diagnostics', 'Cleanup')]
    [string] $Action
)

$ErrorActionPreference = 'Stop'
$logDir = Join-Path $env:RUNNER_TEMP 'com0com-diagnostics'

function Install-Com0com {
    # Signed 3.0 has fresh-install issues: https://docklight.de/dl_prb059/
    $expected = '3bc7313e8577774ce78cff4d1c816faf96cfa48c485bbcc5527d4569f7d5e373'
    $path = 'project/com0com/com0com/2.2.2.0/com0com-2.2.2.0-x64-fre-signed.zip'
    $archive = Join-Path $env:RUNNER_TEMP 'com0com.zip'
    $urls = @("https://netix.dl.sourceforge.net/$path", "https://downloads.sourceforge.net/$path")
    $verified = $false
    foreach ($url in $urls) {
        Write-Host "Downloading com0com from $url"
        Remove-Item $archive -Force -ErrorAction SilentlyContinue
        & curl.exe --fail --location --retry 2 --connect-timeout 20 --max-time 120 --output $archive $url
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $archive)) {
            Write-Warning "Download failed with exit code $LASTEXITCODE"
            continue
        }
        $actual = (Get-FileHash $archive -Algorithm SHA256).Hash
        $size = (Get-Item $archive).Length
        if ($actual -eq $expected) { $verified = $true; break }
        Write-Warning "Discarding unexpected response: $size bytes, SHA-256 $actual"
    }
    if (-not $verified) { throw 'Could not download the verified com0com archive' }
    Write-Host "Verified com0com archive (SHA-256 $actual, $size bytes)"

    $dir = Join-Path $env:RUNNER_TEMP 'com0com'
    Expand-Archive $archive -DestinationPath $dir
    $installer = Join-Path $dir 'com0com-2.2.2.0-x64-fre-signed\setup.exe'
    Write-Host 'Starting the silent com0com installer (timeout: 120 seconds)'
    $result = Start-Process $installer -ArgumentList '/S' -PassThru
    if (-not $result.WaitForExit(120000)) {
        & taskkill.exe /PID $result.Id /T /F
        throw 'com0com installer timed out'
    }
    if ($result.ExitCode -ne 0) { throw "com0com installer failed: $($result.ExitCode)" }
    Write-Host 'com0com installer completed'

    $setup = Get-ChildItem "${env:ProgramFiles(x86)}\com0com", "$env:ProgramFiles\com0com" -Filter setupc.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $setup) { throw 'com0com setupc.exe not found after installation' }
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    if (-not (Test-Path (Join-Path $setup.DirectoryName 'com0com.inf'))) {
        throw 'com0com.inf is missing from the setupc directory'
    }

    # Avoid the publisher-confirmation dialog (0xe0000242) in unattended CI.
    # Validate the catalog and trust only its leaf publisher, never a new root CA.
    $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $setup.DirectoryName 'com0com.cat')
    $signature | Format-List Status, StatusMessage, SignerCertificate |
        Out-String | Tee-Object -FilePath (Join-Path $logDir 'catalog-signature.txt') | Write-Host
    if ($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate) {
        throw 'com0com catalog signature is not valid; refusing to change certificate trust'
    }
    $signer = $signature.SignerCertificate
    if ($signer.Subject -notmatch '(^|,\s*)CN=Steven William Hatchett(,|$)') {
        throw "Unexpected com0com publisher: $($signer.Subject)"
    }
    if (-not (Test-Path "Cert:\LocalMachine\TrustedPublisher\$($signer.Thumbprint)")) {
        Write-Host "Temporarily trusting com0com publisher $($signer.Subject), thumbprint $($signer.Thumbprint)"
        $certFile = Join-Path $logDir 'com0com-publisher.cer'
        Export-Certificate -Cert $signer -FilePath $certFile | Out-Null
        "COM0COM_ADDED_PUBLISHER=$($signer.Thumbprint)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
        Import-Certificate -FilePath $certFile -CertStoreLocation 'Cert:\LocalMachine\TrustedPublisher' | Out-Null
    }

    function Invoke-Setup([string[]] $Arguments) {
        $log = Join-Path $logDir "setupc-$($Arguments[0]).log"
        $options = @('--silent', '--wait', '30', '--output', "`"$log`"")
        Write-Host "Running setupc $($Arguments -join ' ') (timeout: 60 seconds)"
        # setupc resolves INF filenames relative to its working directory.
        $proc = Start-Process -FilePath $setup.FullName -WorkingDirectory $setup.DirectoryName -ArgumentList ($options + $Arguments) -NoNewWindow -PassThru
        try {
            if (-not $proc.WaitForExit(60000)) {
                & taskkill.exe /PID $proc.Id /T /F
                throw "com0com setupc timed out: $($Arguments -join ' ')"
            }
            if ($proc.ExitCode -ne 0) { throw "com0com setupc failed with exit code $($proc.ExitCode)" }
        } finally {
            if (Test-Path $log) { Get-Content $log }
        }
    }
    Invoke-Setup @('install', 'PortName=COM30', 'PortName=COM31')
    Invoke-Setup @('list')
    for ($i = 0; $i -lt 30; $i++) {
        $entry = Get-ItemProperty 'HKLM:\HARDWARE\DEVICEMAP\SERIALCOMM' -ErrorAction SilentlyContinue
        $ports = if ($entry) { @($entry.PSObject.Properties | ForEach-Object { $_.Value }) } else { @() }
        if ($ports -contains 'COM30' -and $ports -contains 'COM31') { return }
        Start-Sleep -Seconds 1
    }
    throw 'COM30 and COM31 did not become available; check driver diagnostics'
}

function Save-Diagnostics {
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    Copy-Item "$env:WINDIR\INF\setupapi.dev.log" $logDir -ErrorAction Continue
    Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.Name -match 'com0com|COM30|COM31' -or $_.PNPDeviceID -match 'com0com' } |
        Format-List Name, Status, ConfigManagerErrorCode, PNPDeviceID |
        Out-File (Join-Path $logDir 'devices.txt')
    Get-Content (Join-Path $logDir 'devices.txt')
}

function Remove-PublisherTrust {
    $thumbprint = $env:COM0COM_ADDED_PUBLISHER
    if (-not $thumbprint) { return }
    if ($thumbprint -notmatch '^[0-9A-Fa-f]{40}$') { throw 'Invalid publisher thumbprint for cleanup' }
    Remove-Item "Cert:\LocalMachine\TrustedPublisher\$thumbprint" -ErrorAction Stop
}

switch ($Action) {
    'Install' { Install-Com0com }
    'Diagnostics' { Save-Diagnostics }
    'Cleanup' { Remove-PublisherTrust }
}
