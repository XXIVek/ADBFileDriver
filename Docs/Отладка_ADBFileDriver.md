# Отладка ADBFileDriver

## Этап 1: MTP Device Management (выполнен)

### Реализованные методы:
1. **ПеречислитьУстройства()** - возвращает JSON со списком MTP устройств
2. **Подключить(имя)** - подключается к устройству по имени
3. **Отключить()** - отключается от устройства

### Технические детали:
- Используется Shell Namespace API для перечисления устройств
- COM инициализация через CoInitializeEx
- GUID класса: `{241D7C96-FF80-11D0-9C8E-02608C9E75FD}`

### JSON формат:
```json
[
  {"Id":"", "Name":"Устройство1"},
  {"Id":"", "Name":"Устройство2"}
]
```

## Логирование
- Лог пишется в `%TEMP%\ADBFileDriver.log`
- DebugView показывает сообщения через OutputDebugStringW

## Сборка
```powershell
.\build.ps1 Release
```

## Расположение DLL
- `1C/Release/Win64/ADBFileDriver_Win64.dll`