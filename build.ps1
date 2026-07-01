# ============================================================
# ADBFileDriver Build Script for Windows
# ============================================================

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  ADBFileDriver Build Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ---------- Проверка зависимостей ----------

# Проверка CMake
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Host "ОШИБКА: CMake не найден. Установите CMake 3.10+." -ForegroundColor Red
    exit 1
}
Write-Host "[OK] CMake найден: $($cmake.Source)" -ForegroundColor Green

# Проверка MSBuild
$msbuild = Get-Command msbuild -ErrorAction SilentlyContinue
if (-not $msbuild) {
    Write-Host "ОШИБКА: MSBuild не найден. Установите Visual Studio 2022." -ForegroundColor Red
    exit 1
}
Write-Host "[OK] MSBuild найден: $($msbuild.Source)" -ForegroundColor Green

# ---------- Настройка путей ----------

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ScriptDir "build"
$OutputDir = Join-Path $ScriptDir "1C/Release"
$BundleSrc = Join-Path $ScriptDir "bundle"
$BundleDst = Join-Path $OutputDir "bundle"

# ---------- Создание директории сборки ----------

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
    Write-Host "[INFO] Создана папка сборки: build/" -ForegroundColor Gray
}

# ---------- Генерация проекта ----------

Write-Host ""
Write-Host "----------------------------------------" -ForegroundColor Yellow
Write-Host "  Генерация проекта CMake" -ForegroundColor Yellow
Write-Host "----------------------------------------" -ForegroundColor Yellow
Write-Host ""

Set-Location $BuildDir

cmake .. -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) {
    Write-Host "ОШИБКА: Генерация проекта не удалась." -ForegroundColor Red
    Set-Location $ScriptDir
    exit 1
}
Write-Host "[OK] Проект Win64 сгенерирован." -ForegroundColor Green

# ---------- Сборка Win64 ----------

Write-Host ""
Write-Host "----------------------------------------" -ForegroundColor Yellow
Write-Host "  Сборка Win64 (Release)" -ForegroundColor Yellow
Write-Host "----------------------------------------" -ForegroundColor Yellow
Write-Host ""

cmake --build . --config Release --target ADBFileDriver_Win64
if ($LASTEXITCODE -ne 0) {
    Write-Host "ОШИБКА: Сборка Win64 не удалась." -ForegroundColor Red
    Set-Location $ScriptDir
    exit 1
}
Write-Host "[OK] Сборка Win64 завершена." -ForegroundColor Green

# ---------- Сборка Win32 ----------

Write-Host ""
Write-Host "----------------------------------------" -ForegroundColor Yellow
Write-Host "  Сборка Win32 (Release)" -ForegroundColor Yellow
Write-Host "----------------------------------------" -ForegroundColor Yellow
Write-Host ""

cmake .. -G "Visual Studio 17 2022" -A Win32
if ($LASTEXITCODE -ne 0) {
    Write-Host "ОШИБКА: Генерация Win32 проекта не удалась." -ForegroundColor Red
    Set-Location $ScriptDir
    exit 1
}

cmake --build . --config Release --target ADBFileDriver_Win32
if ($LASTEXITCODE -ne 0) {
    Write-Host "ОШИБКА: Сборка Win32 не удалась." -ForegroundColor Red
    Set-Location $ScriptDir
    exit 1
}
Write-Host "[OK] Сборка Win32 завершена." -ForegroundColor Green

# ---------- Копирование MANIFEST.xml ----------

Write-Host ""
Write-Host "----------------------------------------" -ForegroundColor Yellow
Write-Host "  Подготовка bundle" -ForegroundColor Yellow
Write-Host "----------------------------------------" -ForegroundColor Yellow
Write-Host ""

if (-not (Test-Path $BundleDst)) {
    New-Item -ItemType Directory -Path $BundleDst | Out-Null
}

if (Test-Path (Join-Path $BundleSrc "MANIFEST.xml")) {
    Copy-Item (Join-Path $BundleSrc "MANIFEST.xml") -Destination $BundleDst -Force
    Write-Host "[OK] MANIFEST.xml скопирован." -ForegroundColor Green
} else {
    Write-Host "ВНИМАНИЕ: MANIFEST.xml не найден в bundle/. Пропущен." -ForegroundColor DarkYellow
}

# ---------- Итоговая информация ----------

Set-Location $ScriptDir

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Сборка завершена успешно!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Выходные файлы:" -ForegroundColor White
Write-Host "    Win64 DLL:  1C/Release/Win64/ADBFileDriver_Win64.dll" -ForegroundColor Gray
Write-Host "    Win32 DLL:  1C/Release/Win32/ADBFileDriver_Win32.dll" -ForegroundColor Gray
Write-Host "    Bundle:     1C/Release/bundle/MANIFEST.xml" -ForegroundColor Gray
Write-Host ""
Write-Host "  Использование в 1С:" -ForegroundColor White
Write-Host '    Драйвер = New("AddIn.ADBFileDriver.ADBFileDriver")' -ForegroundColor Gray
Write-Host '    Message(Драйвер.Версия())' -ForegroundColor Gray
Write-Host ""