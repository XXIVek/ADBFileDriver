# Test ADBFileDriver component
$com = New-Object -ComObject ADBFileDriver.ADBFileDriver
Write-Host "Version: $($com.Version)"
Write-Host "Status: $($com.Status)"
$devices = $com.EnumerateDevices()
Write-Host "Devices: $devices"
$com = $null