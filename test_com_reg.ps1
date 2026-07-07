# Тестирование регистрации COM IPortableDeviceManager

Write-Host "=== Тест 1: Поиск CLSID в реестре ===" -ForegroundColor Cyan
$guid = "{72A2E8D8-1F2-499B-B31A-565B3F6D4F5A}"
$path = "HKCR:\CLSID\$guid\InprocServer32"
if (Test-Path $path) {
    $val = Get-ItemProperty $path
    Write-Host "НАЙДЕН: $path" -ForegroundColor Green
    Write-Host "  DLL = $($val.'(default)')" -ForegroundColor White
    Write-Host "  Thread = $($val.'ThreadingModel')" -ForegroundColor White
} else {
    Write-Host "НЕ НАЙДЕН: $path" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Тест 2: Поиск всех CLSID из PortableDeviceApi.dll ===" -ForegroundColor Cyan
$found = $false
$clses = Get-ChildItem "HKCR:\CLSID" -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.PSChildName -eq "InprocServer32" }
foreach ($cls in $clses) {
    try {
        $dll = (Get-ItemProperty $cls.PSPath -ErrorAction SilentlyContinue).'(default)'
        if ($dll -and $dll.Contains('PortableDevice')) {
            $found = $true
            $parentPath = Split-Path $cls.PSPath -Parent
            $clsid = Split-Path $parentPath -Leaf
            $parentName = (Get-ItemProperty $parentPath -ErrorAction SilentlyContinue).'(default)'
            Write-Host "CLSID: $clsid" -ForegroundColor Yellow
            if ($parentName) { Write-Host "  Name: $parentName" -ForegroundColor White }
            Write-Host "  DLL: $dll" -ForegroundColor White
            Write-Host ""
        }
    } catch {}
}
if (-not $found) {
    Write-Host "НИ ОДИН CLSID не зарегистрирован!" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Тест 3: Попытка создания COM объекта ===" -ForegroundColor Cyan
try {
    $type = [Type]::GetTypeFromProgID("PortableDeviceManager")
    if ($type) {
        $obj = [Activator]::CreateInstance($type)
        Write-Host "Создан через ProgID: PortableDeviceManager" -ForegroundColor Green
        [Runtime.InteropServices.Marshal]::ReleaseComObject($obj) | Out-Null
    } else {
        Write-Host "ProgID не найден в реестре" -ForegroundColor Red
    }
} catch {
    Write-Host ("Ошибка: " + $_.Exception.Message) -ForegroundColor Red
}

try {
    $guid = [Guid]"$guid"
    $type = [Type]::GetTypeFromCLSID($guid)
    if ($type) {
        $obj = [Activator]::CreateInstance($type)
        Write-Host "Создан через CLSID" -ForegroundColor Green
        [Runtime.InteropServices.Marshal]::ReleaseComObject($obj) | Out-Null
    } else {
        Write-Host "CLSID не найден в реестре" -ForegroundColor Red
    }
} catch {
    Write-Host ("Ошибка: " + $_.Exception.Message) -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Готово ===" -ForegroundColor Cyan