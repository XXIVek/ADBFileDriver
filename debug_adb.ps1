# Script for debugging ADBFileDriver via DebugView
# Usage: .\debug_adb.ps1

$DebugViewPath = 'J:\Visual studio prog\DebugView\dbgviewcli64.exe'
$Logfile = (Get-Location).Path + "\ADB_debug_$(Get-Date -Format 'yyyyMMdd_HHmmss').log"

Write-Host "=== ADBFileDriver Debugging ===" -ForegroundColor Cyan
Write-Host "Log file: $Logfile" -ForegroundColor Yellow
Write-Host ""

# Check DebugView existence
if (-not (Test-Path $DebugViewPath)) {
    Write-Host "ERROR: DebugView not found at: $DebugViewPath" -ForegroundColor Red
    Write-Host "Check path to DebugView." -ForegroundColor Yellow
    exit 1
}

# Start DebugView - capture only, save manually or use duration
Write-Host "Starting DebugView..." -ForegroundColor Green
try {
    # Start with duration to auto-save and exit
    $process = Start-Process -FilePath $DebugViewPath -ArgumentList "--accepteula", "--no-banner", "--capture", "--win32", "--log", $Logfile, "--pids", "--duration", "300" -PassThru -ErrorAction Stop
    Write-Host "DebugView started (PID: $($process.Id)), will capture for 300 seconds" -ForegroundColor Green
    Write-Host "Press Enter to stop capture early..." -ForegroundColor Yellow
    Read-Host
    # Kill the process to stop capture
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
} catch {
    Write-Host "ERROR: Cannot start DebugView: $_" -ForegroundColor Red
    exit 1
}

Write-Host "Capture stopped." -ForegroundColor Green

# Wait for file to be written
Start-Sleep -Seconds 3

Write-Host "Results saved to: $Logfile" -ForegroundColor Green

# Show first 30 lines
if (Test-Path $Logfile) {
    Write-Host ""
    Write-Host "=== First 30 lines of log ===" -ForegroundColor Cyan
    Get-Content $Logfile -Head 30
    
    # Count lines
    $lineCount = (Get-Content $Logfile).Count
    Write-Host ""
    Write-Host "Total lines in log: $lineCount" -ForegroundColor Magenta
    
    # Search for ADB-related messages
    $adbMsgs = Select-String -Path $Logfile -Pattern "\[CONSTRUCTOR\]|\[Init\]|\[Done\]|\[CallAsFunc\]|\[Connect\]|\[Disconnect\]|\[EnumerateDevices\]" -CaseSensitive:$false
    if ($adbMsgs) {
        Write-Host ""
        Write-Host "=== ADBFileDriver messages detected ===" -ForegroundColor Green
        $adbMsgs | ForEach-Object { Write-Host $_.Line -ForegroundColor Green }
    } else {
        Write-Host ""
        Write-Host "No ADBFileDriver messages found in log." -ForegroundColor Yellow
        Write-Host "Make sure to:" -ForegroundColor Yellow
        Write-Host "1. Copy AdbWinUsbApi.dll to C:\TMP\ or next to DLL" -ForegroundColor Yellow
        Write-Host "2. Connect ADB device" -ForegroundColor Yellow
        Write-Host "3. Execute code that uses ADBFileDriver" -ForegroundColor Yellow
    }
} else {
    Write-Host "Log file not created." -ForegroundColor Yellow
    Write-Host "Possible reasons:" -ForegroundColor Yellow
    Write-Host "- No OutputDebugString calls were made during capture" -ForegroundColor Yellow
    Write-Host "- DebugView was not running when component was used" -ForegroundColor Yellow
    Write-Host "- DLL not loaded by 1C" -ForegroundColor Yellow
}
