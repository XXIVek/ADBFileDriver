# Руководство по созданию внешних компонент 1С:Предприятие 8.3 (Native API)

## Сжатое руководство для C++ разработчиков

---

## 1. Архитектура компоненты

```
┌─────────────────────────────────────────────┐
│           1С:Предприятие 8.3                 │
│                                             │
│  ПодключитьВнешнююКомпоненту("My.dll",      │
│      "MyObject", ТипВнешнейКомпоненты.Native)│
└──────────────────┬──────────────────────────┘
                   │ Загрузка DLL
                   │ GetClassNames() → "MyObject"
                   │ GetClassObject("MyObject", &pIntf)
                   │ pIntf->setMemManager(memMgr)
                   │ pIntf->Init(addInDef)
                   │ pIntf->RegisterExtensionAs(&name)
┌──────────────────▼──────────────────────────┐
│         My.dll (Native API)                  │
│                                              │
│  CMyObject : public IComponentBase           │
│    ├─ Свойства (GetNProps, GetPropVal...)    │
│    ├─ Методы (GetNMethods, CallAsFunc...)    │
│    └─ Память (через IMemoryManager)          │
└─────────────────────────────────────────────┘
```

---

## 2. Обязательные экспортируемые функции (5 штук)

### 2.1 GetClassNames() — Имена объектов

```cpp
const WCHAR_T* GetClassNames()
{
    // Список имен объектов, разделенных \0, с двойным \0 в конце
    static WCHAR_T names[] = L"MyObject\0";
    return names;
}
```

**КРИТИЧНО:** Двойной `\0` в конце обязателен! 1С парсит строки до двойного `\0`.

### 2.2 GetClassObject() — Создание объекта

```cpp
long GetClassObject(const WCHAR_T* clsName, IComponentBase** pIntf)
{
    if (_wcsicmp(clsName, L"MyObject") == 0) {
        *pIntf = new CMyObject();
        return *pIntf ? 1 : 0;
    }
    return 0;
}
```

### 2.3 DestroyObject() — Удаление объекта

```cpp
long DestroyObject(IComponentBase** pIntf)
{
    if (pIntf && *pIntf) {
        delete *pIntf;
        *pIntf = nullptr;
        return 0;
    }
    return -1;
}
```

### 2.4 SetPlatformCapabilities() — Версия платформы

```cpp
AppCapabilities SetPlatformCapabilities(const AppCapabilities capabilities)
{
    return eAppCapabilitiesLast; // Возвращаем максимальную поддерживаемую версию
}
```

### 2.5 GetAttachType() — Тип подключения

```cpp
long GetAttachType()
{
    return 0; // eCanAttachAny = 0 (любое подключение)
}
```

---

## 3. Класс компоненты — минимальный шаблон

```cpp
#include "ComponentBase.h"

class CMyObject : public IComponentBase
{
private:
    IAddInDefBase* m_pBackConn;   // Связь с 1С
    IMemoryManager* m_pMemMgr;    // Менеджер памяти 1С
    bool m_bInitialized;

public:
    CMyObject() : m_pBackConn(nullptr), m_pMemMgr(nullptr), m_bInitialized(false) {}
    virtual ~CMyObject() {}

    // ===== IComponentBase (обязательные) =====
    bool Init(void* Interface) override;
    void Done() override;
    long GetInfo() override;
    bool setMemManager(void* memManager) override;

    // ===== ILanguageExtender (0 методов, 0 свойств для минимального примера) =====
    long GetNProps() override { return 0; }
    long FindProp(const WCHAR_T* wsPropName) override { return -1; }
    const WCHAR_T* GetPropName(long lPropNum, long lPropAlias) override { return nullptr; }
    bool GetPropVal(const long lPropNum, tVariant* pvarPropVal) override { return false; }
    bool SetPropVal(const long lPropNum, tVariant* pvarPropVal) override { return false; }
    bool IsPropReadable(const long lPropNum) override { return false; }
    bool IsPropWritable(const long lPropNum) override { return false; }

    long GetNMethods() override { return 0; }
    long FindMethod(const WCHAR_T* wsMethodName) override { return -1; }
    const WCHAR_T* GetMethodName(const long lMethodNum, const long lMethodAlias) override { return nullptr; }
    long GetNParams(const long lMethodNum) override { return 0; }
    bool GetParamDefValue(const long lMethodNum, const long lParamNum, tVariant* pvarParamDefValue) override { return true; }
    bool HasRetVal(const long lMethodNum) override { return false; }
    bool CallAsProc(const long lMethodNum, tVariant* paParams, const long lSizeArray) override { return false; }
    bool CallAsFunc(const long lMethodNum, tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray) override { return false; }

    bool RegisterExtensionAs(WCHAR_T** wsExtName) override;
};

// ===== Экспорт функций =====
extern "C" {
    const WCHAR_T* GetClassNames();
    long GetClassObject(const WCHAR_T* clsName, IComponentBase** pIntf);
    long DestroyObject(IComponentBase** pIntf);
    AppCapabilities SetPlatformCapabilities(const AppCapabilities capabilities);
    long GetAttachType();
}
```

---

## 4. Реализация обязательных методов

```cpp
bool CMyObject::Init(void* Interface)
{
    if (Interface == nullptr) return false;
    m_pBackConn = static_cast<IAddInDefBase*>(Interface);
    m_bInitialized = true;
    return true;
}

void CMyObject::Done()
{
    m_pBackConn = nullptr;
    m_pMemMgr = nullptr;
    m_bInitialized = false;
}

long CMyObject::GetInfo()
{
    // Версия: 1.0.0 = 1000
    return 1000;
}

bool CMyObject::setMemManager(void* memManager)
{
    if (memManager == nullptr) return false;
    m_pMemMgr = static_cast<IMemoryManager*>(memManager);
    return true;
}

bool CMyObject::RegisterExtensionAs(WCHAR_T** wsExtName)
{
    if (m_pMemMgr) {
        size_t len = wcslen(L"MyExtension") + 1;
        m_pMemMgr->AllocMemory((void**)wsExtName, (unsigned long)(len * sizeof(WCHAR_T)));
        ConvToShortWchar(wsExtName, L"MyExtension", (uint32_t)len);
    } else {
        *wsExtName = SysAllocString(L"MyExtension");
    }
    return *wsExtName != nullptr;
}
```

---

## 5. Работа со свойствами

```cpp
// Глобальные массивы имен
static const wchar_t* g_PropNamesEN[] = { L"Status", L"Result" };
static const wchar_t* g_PropNamesRU[] = { L"Статус", L"Результат" };

long CMyObject::GetNProps() { return 2; }

long CMyObject::FindProp(const WCHAR_T* wsPropName)
{
    wchar_t* propName = nullptr;
    ConvFromShortWchar(&propName, wsPropName);
    std::wstring wName(propName);
    delete[] propName;

    for (int i = 0; i < 2; i++) {
        if (wcscmp(g_PropNamesEN[i], wName.c_str()) == 0) return i;
        if (wcscmp(g_PropNamesRU[i], wName.c_str()) == 0) return i;
    }
    return -1;
}

const WCHAR_T* CMyObject::GetPropName(long lPropNum, long lPropAlias)
{
    if (lPropNum < 0 || lPropNum > 1) return nullptr;

    const wchar_t* currentName = (lPropAlias == 0) ? g_PropNamesEN[lPropNum] : g_PropNamesRU[lPropNum];
    WCHAR_T* result = nullptr;
    size_t len = wcslen(currentName) + 1;

    if (m_pMemMgr && m_pMemMgr->AllocMemory((void**)&result, (unsigned long)(len * sizeof(WCHAR_T)))) {
        ConvToShortWchar(&result, currentName, (uint32_t)len);
    }
    return result;
}

bool CMyObject::GetPropVal(const long lPropNum, tVariant* pvarPropVal)
{
    switch (lPropNum) {
        case 0: // Status
            TV_VT(pvarPropVal) = VTYPE_PWSTR;
            // Выделить память через m_pMemMgr->AllocMemory()
            // Скопировать значение
            break;
        case 1: // Result
            TV_VT(pvarPropVal) = VTYPE_PWSTR;
            break;
        default:
            TV_VT(pvarPropVal) = VTYPE_EMPTY;
            return false;
    }
    return true;
}

bool CMyObject::SetPropVal(const long lPropNum, tVariant* pvarPropVal)
{
    // false = свойства только для чтения
    return false;
}

bool CMyObject::IsPropReadable(const long lPropNum) { return lPropNum >= 0 && lPropNum <= 1; }
bool CMyObject::IsPropWritable(const long lPropNum) { return false; }
```

---

## 6. Работа с методами

```cpp
static const wchar_t* g_MethodNamesEN[] = { L"Connect", L"Disconnect" };
static const wchar_t* g_MethodNamesRU[] = { L"Подключить", L"Отключить" };

long CMyObject::GetNMethods() { return 2; }

long CMyObject::FindMethod(const WCHAR_T* wsMethodName)
{
    wchar_t* methodName = nullptr;
    ConvFromShortWchar(&methodName, wsMethodName);
    std::wstring wName(methodName);
    delete[] methodName;

    for (int i = 0; i < 2; i++) {
        if (wcscmp(g_MethodNamesEN[i], wName.c_str()) == 0) return i;
        if (wcscmp(g_MethodNamesRU[i], wName.c_str()) == 0) return i;
    }
    return -1;
}

const WCHAR_T* CMyObject::GetMethodName(const long lMethodNum, const long lMethodAlias)
{
    if (lMethodNum < 0 || lMethodNum > 1) return nullptr;
    const wchar_t* currentName = (lMethodAlias == 0) ? g_MethodNamesEN[lMethodNum] : g_MethodNamesRU[lMethodNum];
    // Выделить память через m_pMemMgr->AllocMemory()
    // ... (аналогично GetPropName)
}

long CMyObject::GetNParams(const long lMethodNum)
{
    switch (lMethodNum) {
        case 0: return 0;  // Connect()
        case 1: return 1;  // Disconnect(Идентификатор)
        default: return 0;
    }
}

bool CMyObject::GetParamDefValue(const long lMethodNum, const long lParamNum, tVariant* pvarParamDefValue)
{
    TV_VT(pvarParamDefValue) = VTYPE_EMPTY;
    return true;
}

bool CMyObject::HasRetVal(const long lMethodNum) { return true; }

bool CMyObject::CallAsProc(const long lMethodNum, tVariant* paParams, const long lSizeArray)
{
    // Все методы возвращают значения — вызываем CallAsFunc
    (void)lMethodNum; (void)paParams; (void)lSizeArray;
    return false;
}

bool CMyObject::CallAsFunc(const long lMethodNum, tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray)
{
    try {
        switch (lMethodNum) {
            case 0: // Connect()
                // Логика подключения
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = VARIANT_TRUE;
                return true;
            case 1: // Disconnect()
                // Логика отключения
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = VARIANT_TRUE;
                return true;
            default:
                TV_VT(pvarRetValue) = VTYPE_EMPTY;
                return false;
        }
    } catch (...) {
        if (m_pBackConn) {
            EXCEPINFO info;
            ZeroMemory(&info, sizeof(EXCEPINFO));
            info.wCode = ADDIN_E_FAIL;
            info.bstrSource = SysAllocString(L"MyComponent");
            info.bstrDescription = SysAllocString(L"Ошибка выполнения");
            m_pBackConn->AddError(info.wCode, info.bstrSource, info.bstrDescription, info.scode);
        }
        TV_VT(pvarRetValue) = VTYPE_EMPTY;
        return false;
    }
}
```

---

## 7. Управление памятью — ЗОЛОТЫЕ ПРАВИЛА

### ✅ Правильно:
```cpp
// Выделение через менеджер памяти 1С
WCHAR_T* result = nullptr;
m_pMemMgr->AllocMemory((void**)&result, 100 * sizeof(WCHAR_T));
wcscpy_s(result, 100, L"Hello");
// 1С сама освободит память
```

### ❌ Неправильно:
```cpp
// НИКОГДА так не делайте!
WCHAR_T* result = new WCHAR_T[100];  // Утечка памяти!
WCHAR_T* result = (WCHAR_T*)malloc(100 * sizeof(WCHAR_T));  // Утечка памяти!
BSTR result = SysAllocString(L"Hello");  // Утечка памяти!
```

### Освобождение памяти:
```cpp
if (result) {
    m_pMemMgr->FreeMemory((void**)&result);
    result = nullptr;
}
```

---

## 8. Обработка ошибок

```cpp
void CMyObject::AddErrorTo1C(uint32_t wcode, const wchar_t* source, const wchar_t* descriptor, long code)
{
    if (m_pBackConn) {
        EXCEPINFO info;
        ZeroMemory(&info, sizeof(EXCEPINFO));
        info.wCode = wcode;
        info.bstrSource = SysAllocString(source);
        info.bstrDescription = SysAllocString(descriptor);
        info.scode = code;
        m_pBackConn->AddError(info.wCode, info.bstrSource, info.bstrDescription, info.scode);
        SysFreeString(info.bstrSource);
        SysFreeString(info.bstrDescription);
    }
}

// Использование:
try {
    // Логика метода
} catch (const std::exception& e) {
    std::string msg = std::string("Ошибка: ") + e.what();
    AddErrorTo1C(ADDIN_E_FAIL, L"MyComponent", L"Произошла ошибка", E_FAIL);
    return false;
} catch (...) {
    AddErrorTo1C(ADDIN_E_FAIL, L"MyComponent", L"Неизвестная ошибка", E_FAIL);
    return false;
}
```

---

## 9. Соответствие типов tVariant

| tVariant тип | 1С тип | Поле | Описание |
|--------------|---------|------|----------|
| `VTYPE_EMPTY` | Неопределено | — | Параметр не передан |
| `VTYPE_I4` | Число | `lVal` | Целое (32-bit) |
| `VTYPE_BOOL` | Булево | `bVal` | Истина/Ложь |
| `VTYPE_R8` | Число | `dblVal` | Дробное (64-bit) |
| `VTYPE_PWSTR` | Строка | `pwstrVal` | Unicode-строка |
| `VTYPE_BLOB` | ДвоичныеДанные | `pstrVal` | Двоичные данные |
| `VTYPE_DATE` | Дата | `date` | Дата/время |

---

## 10. CMakeLists.txt для сборки

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyComponent VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/include)

if(WIN32)
    # Win32 (i386)
    add_library(MyComponent_Win32 SHARED src/MyComponent.cpp)
    target_sources(MyComponent_Win32 PRIVATE MyComponent.def)
    set_target_properties(MyComponent_Win32 PROPERTIES
        OUTPUT_NAME MyComponent_Win32
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/1C/Release/Win32"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/1C/Release/Win32"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/1C/Release/Win32"
    )
    target_compile_options(MyComponent_Win32 PRIVATE /utf-8 /EHsc)
    target_link_libraries(MyComponent_Win32 ole32.lib oleaut32.lib shlwapi.lib advapi32.lib)

    # Win64 (x86_64)
    add_library(MyComponent_Win64 SHARED src/MyComponent.cpp)
    target_sources(MyComponent_Win64 PRIVATE MyComponent.def)
    set_target_properties(MyComponent_Win64 PROPERTIES
        OUTPUT_NAME MyComponent_Win64
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/1C/Release/Win64"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/1C/Release/Win64"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/1C/Release/Win64"
    )
    target_compile_options(MyComponent_Win64 PRIVATE /utf-8 /EHsc)
    target_link_libraries(MyComponent_Win64 ole32.lib oleaut32.lib shlwapi.lib advapi32.lib)
endif()
```

---

## 11. DEF-файл (экспорт символов)

```def
EXPORTS
    GetClassNames @1
    GetClassObject @2
    DestroyObject @3
    SetPlatformCapabilities @4
    GetAttachType @5
```

**ВАЖНО:** Не указывайте директиву `LIBRARY` — она вызывает конфликты!

---

## 12. MANIFEST.XML для упаковки

```xml
<?xml version="1.0" encoding="UTF-8"?>
<bundle xmlns="http://v8.1c.ru/8.2/addin/bundle" name="MyComponent">
  <component os="Windows" path="Win32/MyComponent_Win32.dll" type="native" arch="i386" />
  <component os="Windows" path="Win64/MyComponent_Win64.dll" type="native" arch="x86_64" />
</bundle>
```

---

## 13. Чек-лист перед сборкой

```
□ Экспортируются все 5 обязательных функций
□ GetClassNames() возвращает имена с двойным \0 в конце
□ GetClassObject() создает объект и записывает в pIntf
□ DestroyObject() удаляет объект и обнуляет pIntf
□ setMemManager() сохраняет указатель на IMemoryManager
□ Init() сохраняет указатель на IAddInDefBase
□ Все строки, возвращаемые 1С, выделены через AllocMemory()
□ Все исключения перехвачены внутри компоненты
□ Ошибки передаются через AddError()
□ Реализован RegisterExtensionAs()
□ DEF-файл содержит все 5 экспортов
□ Нет директивы LIBRARY в DEF-файле
```

---

## 14. Типичные проблемы и решения

| Проблема | Причина | Решение |
|----------|---------|---------|
| DLL не загружается (Ложь) | GetAttachType закомментирован | Раскомментировать GetAttachType @5 |
| DLL не загружается (Ложь) | LIBRARY не совпадает с именем | Удалить директиву LIBRARY |
| DLL не загружается (Ложь) | Нет двойного \0 в GetClassNames | Добавить `\0` в конце |
| Утечки памяти | new/malloc вместо AllocMemory | Использовать только AllocMemory |
| Вылеты приложения | Исключения выходят за пределы | Обернуть весь код в try-catch |
| Ошибки кодировки | wchar_t (4 байта) на Linux | Преобразовать через ConvToShortWchar |

---

## 15. Пример подключения в 1С

### 15.1. Подключение из файла на диске (с учетом разрядности)

```bsl
// Определение разрядности системы
Функция ПолучитьРазрядностьСистемы()
    СисИнфо = Новый СистемнаяИнформация;
    Если СисИнфо.ТипПлатформы = ТипПлатформы.Windows_x86 Тогда
        Возврат "x86";
    ИначеЕсли СисИнфо.ТипПлатформы = ТипПлатформы.Windows_x86_64 Тогда
        Возврат "x64";
    ИначеЕсли СисИнфо.ТипПлатформы = ТипПлатформы.Linux_x86 Тогда
        Возврат "x86";
    ИначеЕсли СисИнфо.ТипПлатформы = ТипПлатформы.Linux_x86_64 Тогда
        Возврат "x64";
    ИначеЕсли СисИнфо.ТипПлатформы = ТипПлатформы.Linux_ARM64 Тогда
        Возврат "ARM64";
    КонецЕсли;
    Возврат "x64"; // Значение по умолчанию
КонецФункции

// Основная процедура подключения
Процедура ПодключитьКомпонентуИзФайла()
    Перем Драйвер;
    Перем ИмяКомпоненты;
    Перем ПутьКФайлу;
    Перем Разрядность;

    ИмяКомпоненты = "MyObject";
    Разрядность = ПолучитьРазрядностьСистемы();

    // Формирование пути к нужной версии компоненты
    Если Разрядность = "x86" Тогда
        ПутьКФайлу = "C:\MyComponent\MyComponent_Win32.dll";
    Иначе
        ПутьКФайлу = "C:\MyComponent\MyComponent_Win64.dll";
    КонецЕсли;

    // Подключение внешней компоненты
    Попытка
        РезультатПодключения = ПодключитьВнешнююКомпоненту(
            ПутьКФайлу,
            ИмяКомпоненты,
            ТипВнешнейКомпоненты.Native
        );
        Если Не РезультатПодключения Тогда
            Сообщить("Не удалось подключить компоненту: " + ИмяКомпоненты);
            Возврат;
        КонецЕсли;

        // Создание объекта компоненты
        ИмяОбъекта = "AddIn." + ИмяКомпоненты + "." + ИмяКомпоненты;
        Драйвер = Новый(ИмяОбъекта);
        Сообщить("Компонента успешно подключена и создан объект: " + ИмяОбъекта);

        // Работа с компонентой
        // Свойства
        Если Драйвер.Свойство("Статус") Тогда
            Сообщить("Статус: " + Драйвер.Статус);
        КонецЕсли;

        // Методы
        Попытка
            Если Драйвер.Подключить() Тогда
                Сообщить("Успешное подключение к оборудованию!");
            Иначе
                Сообщить("Не удалось подключиться к оборудованию.");
            КонецЕсли;
        Исключение
            Сообщить("Ошибка при вызове метода: " + ОписаниеОшибки());
        КонецПопытки;
    Исключение
        Сообщить("Ошибка при подключении компоненты: " + ОписаниеОшибки());
    КонецПопытки;
КонецПроцедуры
```

### 15.2. Подключение из макета конфигурации

```bsl
Процедура ПодключитьКомпонентуИзМакета()
    Перем Драйвер;
    Перем ИмяКомпоненты;
    Перем ИмяМакета;
    Перем ДвоичныеДанные;

    ИмяКомпоненты = "MyObject";
    ИмяМакета = "МакетВнешнейКомпоненты";

    Попытка
        // Получение макета компоненты из конфигурации
        ДвоичныеДанные = ПолучитьМакет(ИмяМакета);
        Если ДвоичныеДанные = Неопределено Тогда
            Сообщить("Макет не найден: " + ИмяМакета);
            Возврат;
        КонецЕсли;

        // Подключение из двоичных данных
        РезультатПодключения = ПодключитьВнешнююКомпоненту(
            ДвоичныеДанные,
            ИмяКомпоненты,
            ТипВнешнейКомпоненты.Native
        );
        Если Не РезультатПодключения Тогда
            Сообщить("Не удалось подключить компоненту из макета");
            Возврат;
        КонецЕсли;

        // Создание объекта компоненты
        ИмяОбъекта = "AddIn." + ИмяКомпоненты + "." + ИмяКомпоненты;
        Драйвер = Новый(ИмяОбъекта);
        Сообщить("Компонента успешно подключена из макета");

        // Использование компоненты
        // ...
    Исключение
        Сообщить("Ошибка при подключении из макета: " + ОписаниеОшибки());
    КонецПопытки;
КонецПроцедуры
```

### 15.3. Подключение из ZIP-архива

```bsl
Процедура ПодключитьКомпонентуИзАрхива()
    Перем Драйвер;
    Перем ИмяКомпоненты;
    Перем ПутьКАрхиву;

    ИмяКомпоненты = "MyObject";
    ПутьКАрхиву = "C:\MyComponent\MyComponent.zip";

    Попытка
        // Подключение из ZIP-архива
        РезультатПодключения = ПодключитьВнешнююКомпоненту(
            ПутьКАрхиву,
            ИмяКомпоненты,
            ТипВнешнейКомпоненты.Native
        );
        Если Не РезультатПодключения Тогда
            Сообщить("Не удалось подключить компоненту из архива");
            Возврат;
        КонецЕсли;

        // Создание объекта компоненты
        ИмяОбъекта = "AddIn." + ИмяКомпоненты + "." + ИмяКомпоненты;
        Драйвер = Новый(ИмяОбъекта);
        Сообщить("Компонента успешно подключена из архива");
    Исключение
        Сообщить("Ошибка при подключении из архива: " + ОписаниеОшибки());
    КонецПопытки;
КонецПроцедуры
```

### 15.4. Подключение из временного хранилища

```bsl
Процедура ПодключитьКомпонентуИзВременногоХранилища()
    Перем Драйвер;
    Перем ИмяКомпоненты;
    Перем АдресВХранилище;

    ИмяКомпоненты = "MyObject";
    // Предположим, что компонента уже загружена во временное хранилище
    АдресВХранилище = ПоместитьВоВременноеХранилище(ПолучитьМакет("МакетВнешнейКомпоненты"));

    Попытка
        // Подключение из временного хранилища
        РезультатПодключения = ПодключитьВнешнююКомпоненту(
            АдресВХранилище,
            ИмяКомпоненты,
            ТипВнешнейКомпоненты.Native
        );
        Если Не РезультатПодключения Тогда
            Сообщить("Не удалось подключить компоненту из хранилища");
            Возврат;
        КонецЕсли;

        // Создание объекта компоненты
        ИмяОбъекта = "AddIn." + ИмяКомпоненты + "." + ИмяКомпоненты;
        Драйвер = Новый(ИмяОбъекта);
        Сообщить("Компонента успешно подключена из временного хранилища");
    Исключение
        Сообщить("Ошибка при подключении из хранилища: " + ОписаниеОшибки());
    КонецПопытки;
КонецПроцедуры
```

### 15.5. Универсальная функция подключения

```bsl
// Универсальная функция подключения компоненты
Функция ПодключитьВнешнююКомпонентуУниверсально(ИмяКомпоненты, Источник)
    Перем Драйвер;
    Перем РезультатПодключения;
    Перем ИмяОбъекта;

    Если Источник = Неопределено Тогда
        Сообщить("Не указан источник компоненты");
        Возврат Неопределено;
    КонецЕсли;

    Попытка
        // Попытка подключения в зависимости от типа источника
        Если ТипЗнч(Источник) = Тип("Строка") Тогда
            // Путь к файлу или ZIP-архиву
            РезультатПодключения = ПодключитьВнешнююКомпоненту(
                Источник,
                ИмяКомпоненты,
                ТипВнешнейКомпоненты.Native
            );
        ИначеЕсли ТипЗнч(Источник) = Тип("ДвоичныеДанные") Тогда
            // Двоичные данные из макета
            РезультатПодключения = ПодключитьВнешнююКомпоненту(
                Источник,
                ИмяКомпоненты,
                ТипВнешнейКомпоненты.Native
            );
        ИначеЕсли ТипЗнч(Источник) = Тип("Строка") И СтрНайти(Источник, "http") = 1 Тогда
            // Возможно, это URL (требует дополнительной обработки)
            Сообщить("Загрузка по URL не поддерживается напрямую");
            Возврат Неопределено;
        Иначе
            // Попытка интерпретировать как адрес временного хранилища
            РезультатПодключения = ПодключитьВнешнююКомпоненту(
                Источник,
                ИмяКомпоненты,
                ТипВнешнейКомпоненты.Native
            );
        КонецЕсли;

        Если Не РезультатПодключения Тогда
            Сообщить("Не удалось подключить компоненту: " + ИмяКомпоненты);
            Возврат Неопределено;
        КонецЕсли;

        // Создание объекта компоненты
        ИмяОбъекта = "AddIn." + ИмяКомпоненты + "." + ИмяКомпоненты;
        Драйвер = Новый(ИмяОбъекта);
        Возврат Драйвер;
    Исключение
        Сообщить("Ошибка при подключении компоненты: " + ОписаниеОшибки());
        Возврат Неопределено;
    КонецПопытки;
КонецФункции

// Пример использования универсальной функции
Процедура ПримерИспользованияУниверсальнойФункции()
    Перем МойДрайвер;

    // Подключение из файла
    МойДрайвер = ПодключитьВнешнююКомпонентуУниверсально(
        "MyObject",
        "C:\MyComponent\MyComponent_Win64.dll"
    );
    Если МойДрайвер <> Неопределено Тогда
        Сообщить("Компонента успешно подключена из файла");
    КонецЕсли;

    // Подключение из макета
    МойДрайвер = ПодключитьВнешнююКомпонентуУниверсально(
        "MyObject",
        ПолучитьМакет("МакетВнешнейКомпоненты")
    );
    Если МойДрайвер <> Неопределено Тогда
        Сообщить("Компонента успешно подключена из макета");
    КонецЕсли;
КонецПроцедуры
```

### 15.6. Работа с внешними событиями

```bsl
// В модуле формы или объекта конфигурации
Перем МойДрайвер;

Процедура ИнициализацияКомпоненты()
    // Подключение компоненты
    МойДрайвер = ПодключитьВнешнююКомпонентуУниверсально(
        "MyObject",
        ПолучитьМакет("МакетВнешнейКомпоненты")
    );
    Если МойДрайвер <> Неопределено Тогда
        // Настройка обработки внешних событий
        // Компонента будет вызывать ОбработкаВнешнегоСобытия
        Сообщить("Компонента готова к работе");
    КонецЕсли;
КонецПроцедуры

// Обработчик внешних событий
Процедура ОбработкаВнешнегоСобытия(Источник, Событие, Данные)
    Если Источник = "MyObject" Тогда
        Если Событие = "DataReceived" Тогда
            // Обработка получения данных
            Сообщить("Получены данные: " + Данные);
        ИначеЕсли Событие = "ConnectionStateChanged" Тогда
            // Обработка изменения состояния подключения
            Сообщить("Состояние подключения: " + Данные);
        ИначеЕсли Событие = "Error" Тогда
            // Обработка ошибок компоненты
            Сообщить("Ошибка компоненты: " + Данные);
        КонецЕсли;
    КонецЕсли;
КонецПроцедуры
```

### 15.7. Проверка доступности методов и свойств

```bsl
Процедура ПроверитьВозможностиКомпоненты(Драйвер, ИмяКомпоненты)
    Если Драйвер = Неопределено Тогда
        Сообщить("Компонента не подключена");
        Возврат;
    КонецЕсли;

    Сообщить("=== Проверка возможностей компоненты: " + ИмяКомпоненты + " ===");

    // Проверка свойств через метаданные
    Попытка
        // Проверка наличия свойства Статус
        Если Драйвер.Свойство("Статус") Тогда
            Сообщить("Свойство 'Статус': доступно");
        Иначе
            Сообщить("Свойство 'Статус': недоступно");
        КонецЕсли;

        // Проверка наличия свойства Версия
        Если Драйвер.Свойство("Версия") Тогда
            Версия = Драйвер.Версия;
            Сообщить("Версия компоненты: " + Версия);
        КонецЕсли;
    Исключение
        Сообщить("Ошибка при проверке свойств: " + ОписаниеОшибки());
    КонецПопытки;

    // Проверка методов
    Попытка
        // Проверка метода Подключить
        Попытка
            Если Драйвер.Подключить() Тогда
                Сообщить("Метод 'Подключить': выполнен успешно");
            КонецЕсли;
        Исключение
            Сообщить("Метод 'Подключить': недоступен или ошибка выполнения");
        КонецПопытки;

        // Проверка метода ПолучитьИнформацию
        Попытка
            Информация = Драйвер.ПолучитьИнформацию();
            Сообщить("Информация: " + Информация);
        Исключение
            Сообщить("Метод 'ПолучитьИнформацию': недоступен");
        КонецПопытки;
    Исключение
        Сообщить("Ошибка при проверке методов: " + ОписаниеОшибки());
    КонецПопытки;
КонецПроцедуры
```

### 15.8. Корректное отключение компоненты

```bsl
Процедура ОтключитьКомпоненту(Драйвер)
    Если Драйвер = Неопределено Тогда
        Возврат;
    КонецЕсли;

    Попытка
        // Попытка корректного завершения работы
        Если Драйвер.Свойство("Подключено") И Драйвер.Подключено Тогда
            Драйвер.Отключить();
        КонецЕсли;
        Сообщить("Компонента корректно отключена");
    Исключение
        Сообщить("Ошибка при отключении компоненты: " + ОписаниеОшибки());
    КонецПопытки;

    // Освобождение ссылки на объект
    Драйвер = Неопределено;
КонецПроцедуры

// Использование при закрытии формы
Процедура ПриЗакрытии()
    ОтключитьКомпоненту(МойДрайвер);
КонецПроцедуры
```

### 15.9. Полный пример использования компоненты

```bsl
// Переменные модуля
Перем МойДрайвер;
Перем ИмяКомпоненты = "MyObject";

// Основная процедура работы с компонентой
Процедура ОсновнаяПроцедура()
    // 1. Подключение компоненты
    МойДрайвер = ПодключитьВнешнююКомпонентуУниверсально(
        ИмяКомпоненты,
        ПолучитьМакет("МакетВнешнейКомпоненты")
    );
    Если МойДрайвер = Неопределено Тогда
        Сообщить("Не удалось подключить компоненту");
        Возврат;
    КонецЕсли;

    // 2. Проверка возможностей
    ПроверитьВозможностиКомпоненты(МойДрайвер, ИмяКомпоненты);

    // 3. Основная работа с компонентой
    Попытка
        // Подключение к оборудованию
        Если МойДрайвер.Подключить() Тогда
            Сообщить("Успешное подключение к оборудованию");

            // Выполнение операций
            Результат = МойДрайвер.ВыполнитьОперацию("Параметр1", "Параметр2");
            Сообщить("Результат операции: " + Результат);

            // Получение статуса
            Если МойДрайвер.Свойство("Статус") Тогда
                Сообщить("Текущий статус: " + МойДрайвер.Статус);
            КонецЕсли;

            // Отключение от оборудования
            МойДрайвер.Отключить();
            Сообщить("Отключено от оборудования");
        Иначе
            Сообщить("Не удалось подключиться к оборудованию");
        КонецЕсли;
    Исключение
        Сообщить("Ошибка при работе с компонентой: " + ОписаниеОшибки());
    КонецПопытки;

    // 4. Корректное отключение компоненты
    ОтключитьКомпоненту(МойДрайвер);
КонецПроцедуры
```

### Основные моменты подключения:

| Пункт | Описание |
|-------|----------|
| **Правильный метод** | Использовать `ПодключитьВнешнююКомпоненту()`, а не `ЗагрузитьВнешнююКомпоненту()` |
| **Учет разрядности** | Определять разрядность системы и загружать соответствующую версию компоненты |
| **Создание объекта** | После успешного подключения создавать объект через `Новый("AddIn.Имя.Имя")` |
| **Обработка ошибок** | Всегда обрабатывать исключения при подключении и использовании |
| **Проверка успеха** | Проверять возвращаемое значение метода подключения |
| **Работа со свойствами** | Использовать метод `Свойство()` для проверки доступности свойств |
| **Внешние события** | Реализовывать процедуру `ОбработкаВнешнегоСобытия()` для асинхронных событий |
| **Корректное отключение** | Освобождать ресурсы при завершении работы |

---

## 16. Ограничения сервера приложений

| Функция | Работает на сервере? |
|---------|----------------------|
| AddError() | ✅ Да |
| setMemManager() | ✅ Да |
| GetPlatformInfo() | ✅ Да |
| ExternalEvent() | ❌ Нет |
| Confirm()/Alert() | ❌ Нет |
| SetStatusLine() | ❌ Нет |
| Write() | ❌ Нет |

---

*Создано на основе документации и практического опыта разработки ADBFileDriver для 1С:Предприятие 8.3*
*Последнее обновление: 29.06.2026*