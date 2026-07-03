# Check AdbWinUsbApi.dll architecture
# Usage: .\check_dll_arch.ps1 [path]

param(
    [string]$Path = "C:\TMP\AdbWinUsbApi.dll"
)

Write-Host "=== Check DLL Architecture ===" -ForegroundColor Cyan
Write-Host "File: $Path" -ForegroundColor Yellow

if (-not (Test-Path $Path)) {
    Write-Host "ERROR: File not found: $Path" -ForegroundColor Red
    exit 1
}

# Get file size
$size = (Get-Item $Path).Length
Write-Host "Size: $size bytes" -ForegroundColor Green

# Read PE header to determine architecture
try {
    $pe = [IO.File]::ReadAllBytes($Path)
    
    if ($pe.Length -lt 0x4E) {
        Write-Host "ERROR: File too small for PE file" -ForegroundColor Red
        exit 1
    }
    
    # COFF Header is at offset 0x4C
    $machine = [BitConverter]::ToUInt16($pe, 0x4C)
    
    $arch = switch ($machine) {
        0x14C { "x86 (32 bit)" }
        0x8664 { "x64 (64 bit)" }
        0x1C0 { "ARM" }
        0x0184 { "ARM64" }
        default { "Unknown (0x$([Convert]::ToString($machine, 16)))" }
    }
    
    if ($arch -like "*x64*") {
        $color = "Green"
    } elseif ($arch -like "*Unknown*") {
        $color = "Red"
    } else {
        $color = "Yellow"
    }
    Write-Host "Architecture: $arch" -ForegroundColor $color
    
    # Check compatibility with our component
    if ($arch -like "*x64*") {
        Write-Host "Status: OK - compatible with ADBFileDriver_Win64.dll" -ForegroundColor Green
    } elseif ($arch -like "*Unknown*") {
        Write-Host "Status: ERROR - File is NOT a valid DLL!" -ForegroundColor Red
        Write-Host "The file has invalid PE header (machine=0x21cd)" -ForegroundColor Red
        Write-Host "This is NOT a real AdbWinUsbApi.dll!" -ForegroundColor Red
        Write-Host ""
        Write-Host "Solution:" -ForegroundColor Yellow
        Write-Host "1. Delete the fake file:" -ForegroundColor Yellow
        Write-Host "   Remove-Item 'C:\TMP\AdbWinUsbApi.dll' -Force" -ForegroundColor Yellow
        Write-Host "2. Get real AdbWinUsbApi.dll from:" -ForegroundColor Yellow
        Write-Host "   - Android SDK Platform-Tools:" -ForegroundColor Yellow
        Write-Host "     https://developer.android.com/studio/command-line/adb" -ForegroundColor Yellow
        Write-Host "   - Or copy from another Windows machine:" -ForegroundColor Yellow
        Write-Host "     C:\Windows\System32\AdbWinUsbApi.dll" -ForegroundColor Yellow
        Write-Host "     C:\Windows\SysWOW64\AdbWinUsbApi.dll (for x86)" -ForegroundColor Yellow
        Write-Host "3. Copy x64 version to:" -ForegroundColor Yellow
        Write-Host "   C:\TMP\AdbWinUsbApi.dll" -ForegroundColor Yellow
        Write-Host "   J:\Visual studio prog\ADBFileDriver\1C\Release\Win64\adb\AdbWinUsbApi.dll" -ForegroundColor Yellow
    } else {
        Write-Host "Status: NO - need x64 version for ADBFileDriver_Win64.dll" -ForegroundColor Red
        Write-Host "Solution: Copy x64 version of AdbWinUsbApi.dll to:" -ForegroundColor Yellow
        Write-Host "  1. C:\TMP\AdbWinUsbApi.dll" -ForegroundColor Yellow
        Write-Host "  2. J:\Visual studio prog\ADBFileDriver\1C\Release\Win64\adb\AdbWinUsbApi.dll" -ForegroundColor Yellow
    }
} catch {
    Write-Host "ERROR reading file: $_" -ForegroundColor Red
}