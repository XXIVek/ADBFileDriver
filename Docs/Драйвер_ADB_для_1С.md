# Драйвер ADBFileDriver для 1С — История изменений

## Версия 2.0.0.1 (03.07.2026)

### Текущие изменения

- **Переписано перечисление устройств через SetupAPI** (вместо WMI):
  - `SetupDiGetClassDevsW(NULL, L"USB", NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES)` — получение всех USB устройств
  - `SetupDiEnumDeviceInfo()` — перебор устройств
  - `CM_Get_Device_IDW()` — получение PNPDeviceID напрямую из PnP manager
  - `CM_Get_DevNode_Status()` — проверка статуса устройства
  - `SetupDiGetDeviceRegistryPropertyW()` — получение FriendlyName

- **Детальное пошаговое логирование** (STEP 0-6):
  - STEP 0: Начало работы
  - STEP 1: SetupAPI инициализация
  - STEP 2.0-2.5: Каждое устройство с контрольными значениями
  - STEP 3: Итоговая статистика: `total=X, usb=Y, androidVid=Z, connected=W, mtp=V`
  - STEP 4: JSON результат
  - STEP 5: ADB проверка
  - STEP 6: ФИНАЛЬНЫЙ результат

- **Добавлен список Android Vendor ID (24 производителя):**
  - `2717` — Xiaomi / Redmi / POCO
  - `04E8` — Samsung
  - `18D1` — Google / Pixel
  - `1BBB` — OPPO
  - `2A47` — Vivo
  - `34E4` — Realme
  - `22B8` — Motorola
  - `1004` — LG
  - `24E3`, `2A70` — OnePlus
  - `0FCE`, `05C6` — Sony
  - `12D1`, `09D0` — Huawei / Honor
  - И другие

- **Определение MTP устройств:**
  - Проверка по ClassGUID: `{eec5ad98-8080-425f-922a-dabf3de3f69a}`
  - Проверка по PNPDeviceID: `SWD\WPDBUSENUM`, `MTP\`

### Исправления

- **Исправлена проблема с пустым списком устройств**: исправлена обработка `RPC_E_CHANGED_MODE` при инициализации COM
- **Исправлен GUID `IID_IPortableDeviceManager`**: заменён на правильный из `portabledevicemanager.h`
- **Улучшена работа с COM**: добавлена правильная обработка `CoInitializeEx` когда COM уже инициализирован

### Технические детали

- `CoInitializeEx` возвращает `RPC_E_CHANGED_MODE` когда COM уже инициализирован с другой моделью потоков
- Это НЕ ошибка - код продолжает работать корректно
- Нужно проверять `hr != RPC_E_CHANGED_MODE` вместо `FAILED(hr)`

## Версия 2.0.0.0 (02.07.2026)

### Изменения

- Переписана компонента для работы с MTP устройствами через Shell Namespace API
- Удалена зависимость от AdbWinApi.dll
- Реализованы методы:
  - ПеречислитьУстройства() - возвращает JSON со списком MTP устройств
  - Подключить(имя) - подключается к устройству
  - Отключить() - отключается от устройства

### Технические детали

- Используется Windows Shell Namespace API
- COM инициализация через CoInitializeEx
- Линковка с shell32.lib, ole32.lib, shlwapi.lib

## Версия 1.0.0.1 (2026.06.15)

### Изменения

- Добавлена отладка через OutputDebugStringW
- Исправлена загрузка AdbWinApi.dll
- Порядок поиска: C:\TMP\ → adb\ → ADB_64\ → ADB_32\ → PATH

## Версия 1.0.0.0 (2026.06.01)

### Начальная версия

- Прямой USB доступ через AdbWinApi.dll
