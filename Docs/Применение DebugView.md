# Применение DebugView в проекте ADBFileDriver

## Общие сведения

DebugView — это утилита от Sysinternals для мониторинга отладочного вывода на локальной системе или любом удалённом компьютере по TCP/IP. Она захватывает:

- **Win32 OutputDebugString** — вызовы из приложений Windows
- **Kernel-mode DbgPrint** — вызовы из драйверов ядра Windows

Для работы с драйвером ADBFileDriver DebugView позволяет отслеживать отладочные сообщения, генерируемые через функции `OutputDebugStringW()` и `OutputDebugStringA()`.

## Требования

- Windows 10 version 1809 (build 17763) / Windows Server 2019 или позже
- Права администратора для захвата kernel-mode вывода
- DebugView v5.0+ (dbgview.exe) или DbgViewCLI (dbgviewcli.exe) для автоматизации

## Интеграция с ADBFileDriver

### 1. Отладочный вывод в ADBFileDriver

В проекте ADBFileDriver используются функции отладки через `OutputDebugStringW()`. Пример использования:

```cpp
// Пример отладочного вывода в ADBFileDriver
void LogDebugMessage(const wchar_t* message) {
    OutputDebugStringW(message);
}
```

### 2. Запуск DebugView для мониторинга

#### GUI версия:
```
dbgview.exe
```

#### Командная строка (автоматизация):
```powershell
# Захват Win32 OutputDebugString
dbgviewcli.exe --no-banner --capture --win32 --duration 60

# Захват с фильтрацией по процессу
dbgviewcli.exe --no-banner --capture --win32 --process-filter "1CEnterprise" --duration 120

# Захват с сохранением в файл
dbgviewcli.exe --no-banner --capture --win32 --log "adb_debug.log" --duration 300
```

### 3. Параметры для работы с ADBFileDriver

#### Основные параметры:
- `--capture` (`-c`) — включить захват
- `--win32` (`-w`) — захват Win32 OutputDebugString
- `--kernel` (`-k`) — захват kernel-mode вывода (требует администратора)
- `--no-banner` — убрать баннер для чистого вывода
- `--pids` — показывать ID процесса

#### Фильтрация:
- `--filter <pattern>` — включить только совпадающие строки
- `--exclude <pattern>` — исключить совпадающие строки
- `--pid-filter <pid>` — захват только конкретного процесса
- `--process-filter <name>` — захват по имени процесса

### 4. Примеры использования

#### Мониторинг ADBFileDriver из 1С:
```powershell
# Захват отладки для процесса 1C
dbgviewcli.exe --no-banner --capture --win32 --process-filter "1CV8" --duration 300 --log "1C_ADB_debug.log"
```

#### Мониторинг с фильтром по ключевым словам:
```powershell
# Фильтрация только сообщений ADB
dbgviewcli.exe --no-banner --capture --win32 --filter "*ADB*" --exclude "*system*" --duration 60
```

#### Бесконечный захват с ручным остановом:
```powershell
dbgviewcli.exe --no-banner --capture --win32 --pids
```

### 5. Анализ результатов

#### Просмотр захваченных сообщений:
1. Запустите DebugView с нужными параметрами
2. Выполните действия в приложении 1С с использованием ADBFileDriver
3. Остановите захват
4. Сохраните результаты: `Файл -> Сохранить`

#### Автоматизированный анализ:
```powershell
# Захват с авто-остановом по количеству строк
dbgviewcli.exe --no-banner --capture --win32 --max-lines 1000 --format csv --log "result.csv"
```

## Рекомендации по отладке ADBFileDriver

### 1. Отладка подключения ADB устройств

```powershell
# Захват сообщений об устройствах
dbgviewcli.exe --no-banner --capture --win32 --filter "*ADB*Device*" --duration 120
```

### 2. Отладка работы с файлами

```powershell
# Захват операций с файлами
dbgviewcli.exe --no-banner --capture --win32 --filter "*File*" --exclude "*system*" --duration 120
```

### 3. Отладка ошибок

```powershell
# Захват только ошибок
dbgviewcli.exe --no-banner --capture --win32 --filter "*ERROR*" --filter "*Error*" --duration 60
```

## Интеграция с процессами 1С

### 1. Определение PID процесса 1С

```powershell
# Получение PID процесса 1С
Get-Process 1CV8 | Select-Object Id, ProcessName
```

### 2. Захват отладки для конкретного процесса 1С

```powershell
# Захват с фильтрацией по PID
$pid = (Get-Process 1CV8 -ErrorAction SilentlyContinue).Id
if ($pid) {
    dbgviewcli.exe --no-banner --capture --win32 --pid-filter $pid --duration 300 --log "1C_debug_$pid.log"
}
```

## Отладка с kernel-mode выводом

### 1. Захват с правами администратора

```powershell
# Запуск от администратора
dbgviewcli.exe --no-banner --capture --kernel --win32 --duration 120
```

### 2. Захват boot-time отладки

```powershell
# Включение boot logging (требует администратора)
dbgviewcli.exe --boot-enable

# Проверка статуса
dbgviewcli.exe --boot-status

# Отключение
dbgviewcli.exe --boot-disable
```

## Автоматизация в скриптах PowerShell

### 1. Базовый скрипт отладки

```powershell
# DebugCapture.ps1
$Logfile = "ADB_debug_$(Get-Date -Format 'yyyyMMdd_HHmmss').log"

Write-Host "Запуск захвата отладки..."
$process = Start-Process -FilePath "dbgviewcli.exe" -ArgumentList "--no-banner", "--capture", "--win32", "--pids", "--log", $Logfile -PassThru -Wait

# Выполнение тестовых действий
Start-Sleep -Seconds 30

# Остановка захвата
Stop-Process -Id $process.Id -Force

Write-Host "Результаты сохранены в: $Logfile"
```

### 2. Продвинутый скрипт с фильтрацией

```powershell
# AdvancedDebugCapture.ps1
function Invoke-ADBCapture {
    param(
        [int]$Duration = 60,
        [string]$Filter = "*",
        [string]$ProcessName = "1CV8"
    )
    
    $LogFile = "ADB_$(Get-Date -Format 'yyyyMMdd_HHmmss').log"
    $PID = (Get-Process $ProcessName -ErrorAction SilentlyContinue).Id
    
    if (-not $PID) {
        Write-Warning "Процесс $ProcessName не найден"
        return
    }
    
    $Args = @(
        "--no-banner",
        "--capture",
        "--win32",
        "--pid-filter", $PID.ToString(),
        "--filter", $Filter,
        "--duration", $Duration.ToString(),
        "--log", $LogFile,
        "--format", "csv"
    )
    
    Write-Host "Захват отладки для процесса $ProcessName (PID: $PID)..."
    Start-Process -FilePath "dbgviewcli.exe" -ArgumentList $Args -Wait
    
    Write-Host "Захват завершён. Файл: $LogFile"
}

# Использование
Invoke-ADBCapture -Duration 120 -Filter "*ADB*" -ProcessName "1CV8"
```

## Решение проблем

### 1. Нет захвата Win32 вывода

- Убедитесь, что включён параметр `--win32`
- Проверьте, что процесс запущен в той же сессии
- Для захвата из сессии 0 используйте `--global`

### 2. Нет захвата kernel-mode вывода

- Запустите от имени администратора
- Включите параметр `--kernel`
- Убедитесь, что драйвер Dbgv.sys загружен

### 3. Пропуск сообщений

- Увеличьте буфер в GUI версии
- Используйте `--duration` для ограниченного захвата
- Проверьте фильтрацию

## Ссылки

- DebugView: https://docs.microsoft.com/en-us/sysinternals/downloads/debugview
- DbgViewCLI: Встроенный в DebugView v5.0+
- Sysinternals: https://docs.microsoft.com/en-us/sysinternals/

## Примечания

1. DebugViewCLI (dbgviewcli.exe) предпочтительнее для автоматизации в PowerShell
2. Для работы с 1С используйте фильтрацию по PID для точного захвата
3. Формат CSV удобен для последующей обработки результатов
4. Всегда используйте `--no-banner` для чистого вывода в скриптах