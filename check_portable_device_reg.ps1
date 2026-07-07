# Проверка регистрации COM-классов из PortableDeviceApi.dll

Write-Host "=== Проверка CLSID IPortableDeviceManager ===" -ForegroundColor Cyan
$clsidPM = "{72A2E8D8-1F2-499B-B31A-565B3F6D4F5A}"
$pathPM = "HKCR:\CLSID\$clsidPM\InprocServer32"
if (Test-Path $pathPM) {
    $val = Get-ItemProperty $pathPM
    Write-Host "НАЙДЕН: $pathPM" -ForegroundColor Green
    Write-Host "  (Default) = $($val.'(default)')" -ForegroundColor White
} else {
    Write-Host "НЕ НАЙДЕН: $pathPM" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Проверка всех CLSID из PortableDeviceApi.dll ===" -ForegroundColor Cyan

# Получаем все CLSID с InprocServer32, указывающими на PortableDeviceApi.dll
$clses = Get-ChildItem "HKCR:\CLSID" -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.PSChildName -eq "InprocServer32" }
foreach ($cls in $clses) {
    try {
        $val = Get-ItemProperty $cls.PSPath -ErrorAction SilentlyContinue
        if ($val -and $val.'(default)' -like "*PortableDeviceApi*") {
            $parentPath = Split-Path $cls.PSPath -Parent
            $clsid = Split-Path $parentPath -Leaf
            $parentName = (Get-ItemProperty $parentPath -ErrorAction SilentlyContinue).'(default)'
            Write-Host "CLSID: $clsid" -ForegroundColor Yellow
            if ($parentName) { Write-Host "  Name: $parentName" -ForegroundColor White }
            Write-Host "  DLL: $($val.'(default)')" -ForegroundColor White
            Write-Host "  ThreadingModel: $($val.'ThreadingModel')" -ForegroundColor White
            Write-Host ""
        }
    } catch {}
}

Write-Host "=== Проверка ProgID IPortableDeviceManager ===" -ForegroundColor Cyan
$progidPath = "HKCR:\IPortableDeviceManager"
if (Test-Path $progidPath) {
    $val = Get-ItemProperty $progidPath
    Write-Host "НАЙДЕН: $progidPath" -ForegroundColor Green
    Write-Host "  (Default) = $($val.'(default)')" -ForegroundColor White
} else {
    Write-Host "НЕ НАЙДЕН: IPortableDeviceManager ProgID" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Проверка IPortableDevice ===" -ForegroundColor Cyan
$clsidPD = "{08340574-9DB-4B6-B1E2-83B2DA7A0991}"
$pathPD = "HKCR:\CLSID\$clsidPD\InprocServer32"
if (Test-Path $pathPD) {
    $val = Get-ItemProperty $pathPD
    Write-Host "НАЙДЕН: $pathPD" -ForegroundColor Green
    Write-Host "  (Default) = $($val.'(default)')" -ForegroundColor White
} else {
    Write-Host "НЕ НАЙДЕН: $pathPD" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Готово ===" -ForegroundColor Cyan