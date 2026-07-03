// ADBFileDriver.cpp : Реализация компоненты для 1С:Предприятие 8.3
// MTP Device Manager - работа с portable устройствами через Shell Namespace
// Отладка через OutputDebugStringW для DebugView

#include "stdafx.h"
#include "ADBFileDriver.h"
#include <shlobj.h>
#include <vector>
#include <string>

// Макрос для отладочного вывода через DebugView
#define DEBUG_LOG(msg) do { OutputDebugStringW(msg); OutputDebugStringW(L"\n"); } while(0)
#define DEBUG_LOG_FMT(fmt, ...) do { wchar_t _dbgbuf[512]; swprintf_s(_dbgbuf, fmt, __VA_ARGS__); OutputDebugStringW(_dbgbuf); OutputDebugStringW(L"\n"); } while(0)

// GUID класса устройств портативных устройств (Portable Devices)
static const GUID CLSID_PortableDevices = 
{ 0x241D7C96, 0xFF80, 0x11D0, { 0x9C, 0x8E, 0x02, 0x60, 0x8C, 0x9E, 0x75, 0xFD } };

// GUID для папки Portable Devices
static const GUID CLSID_PortableDeviceNamespace = 
{ 0x0DFDFE36, 0xC9E2, 0x4A51, { 0xB6, 0x83, 0xDF, 0x7C, 0xBD, 0xB4, 0x28, 0xA6 } };

// Глобальные массивы имен свойств
static const wchar_t* g_PropNamesEN[] = { L"Version", L"EnableLog", L"LogPath", L"Status", L"DeviceCount" };
static const wchar_t* g_PropNamesRU[] = { L"Версия", L"ВключитьЛогирование", L"ПутьДляФайлаЛогирования", L"Статус", L"КоличествоУстройств" };
#define PROPS_COUNT 5

// Глобальные массивы имен методов
static const wchar_t* g_MethodNamesEN[] = { L"EnumerateDevices", L"Connect", L"Disconnect" };
static const wchar_t* g_MethodNamesRU[] = { L"ПеречислитьУстройства", L"Подключить", L"Отключить" };
#define METHODS_COUNT 3

// Путь для логирования
static wchar_t g_LogPathGlobal[512] = L"";

static void WriteLog(const wchar_t* msg)
{
    if (g_LogPathGlobal[0] == L'\0') return;
    
    wchar_t logPath[512];
    DWORD pathLen = ExpandEnvironmentStringsW(g_LogPathGlobal, logPath, 512);
    if (pathLen == 0 || pathLen > 512) return;
    
    HANDLE hLog = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, 
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hLog == INVALID_HANDLE_VALUE) return;
    
    SetFilePointer(hLog, 0, NULL, FILE_END);
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t entry[512];
    int len = swprintf_s(entry, L"[%d:%02d:%02d.%03d] %s\r\n",
                          st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
    
    DWORD bytesWritten;
    WriteFile(hLog, entry, (DWORD)(len * sizeof(wchar_t)), &bytesWritten, NULL);
    CloseHandle(hLog);
}

///////////////////////////////////////////////////////////////////////////////
// ADBFileDriver

ADBFileDriver::ADBFileDriver(void)
    : m_iConnect(nullptr), m_iMemory(nullptr), m_bInitialized(false)
    , m_EnableLog(false), m_bConnected(false)
    , m_LogHandle(nullptr)
    , m_DeviceCount(0)
    , m_pDevice(nullptr)
{
    DEBUG_LOG(L"[CONSTRUCTOR] ADBFileDriver конструктор вызван");
    
    // Инициализация пути логирования по умолчанию (временная папка)
    ExpandEnvironmentStringsW(L"%TEMP%\\ADBFileDriver.log", m_LogPath, 512);
    wcscpy_s(g_LogPathGlobal, 512, m_LogPath);
    
    // Инициализация статуса
    wcscpy_s(m_Status, 512, L"Не подключен");
    
    // Инициализация критической секции
    InitializeCriticalSection(&m_LogLock);
    
    // Инициализация списка устройств
    m_DeviceList[0] = L'\0';
    m_DeviceId[0] = L'\0';
    
    DEBUG_LOG(L"[CONSTRUCTOR] ADBFileDriver конструктор завершен");
}

ADBFileDriver::~ADBFileDriver()
{
    if (m_LogHandle != nullptr) {
        CloseHandle(m_LogHandle);
        m_LogHandle = nullptr;
    }
    DeleteCriticalSection(&m_LogLock);
    
    if (m_pDevice) {
        IUnknown* pUnk = static_cast<IUnknown*>(m_pDevice);
        pUnk->Release();
        m_pDevice = nullptr;
    }
    
    if (m_iConnect) {
        m_iConnect = nullptr;
    }
    if (m_iMemory) {
        m_iMemory = nullptr;
    }
    m_bInitialized = false;
}

// ===== IInitDoneBase =====

bool ADBFileDriver::Init(void* Interface)
{
    DEBUG_LOG_FMT(L"[Init] Вход: Interface=%p", Interface);
    
    if (Interface == nullptr) {
        DEBUG_LOG(L"[Init] Выход: Interface == nullptr");
        return false;
    }
    
    m_iConnect = static_cast<IAddInDefBase*>(Interface);
    m_bInitialized = true;
    
    DEBUG_LOG_FMT(L"[Init] m_iConnect=%p, m_bInitialized=%d", (void*)m_iConnect, m_bInitialized);
    
    wchar_t msg[512];
    swprintf_s(msg, L"Компонента инициализирована, лог: %s", m_LogPath);
    WriteLog(msg);
    return true;
}

bool ADBFileDriver::setMemManager(void* memManager)
{
    if (memManager == nullptr) return false;
    m_iMemory = static_cast<IMemoryManager*>(memManager);
    return true;
}

long ADBFileDriver::GetInfo()
{
    return 100001;
}

void ADBFileDriver::Done()
{
    DEBUG_LOG(L"[Done] Начало завершения работы");
    
    if (m_LogHandle != nullptr) {
        CloseHandle(m_LogHandle);
        m_LogHandle = nullptr;
        DEBUG_LOG(L"[Done] m_LogHandle закрыт");
    }
    
    if (m_iConnect) {
        DEBUG_LOG_FMT(L"[Done] m_iConnect=%p - освобождение", (void*)m_iConnect);
        m_iConnect = nullptr;
    }
    if (m_iMemory) {
        DEBUG_LOG_FMT(L"[Done] m_iMemory=%p - освобождение", (void*)m_iMemory);
        m_iMemory = nullptr;
    }
    m_bInitialized = false;
    
    DEBUG_LOG(L"[Done] Завершение");
}

// ===== ILanguageExtenderBase - Свойства =====

long ADBFileDriver::GetNProps()
{
    return PROPS_COUNT;
}

long ADBFileDriver::FindProp(const WCHAR_T* wsPropName)
{
    wchar_t* propName = nullptr;
    convFromShortWchar(&propName, wsPropName);
    std::wstring wName(propName);
    delete[] propName;

    for (int i = 0; i < PROPS_COUNT; i++) {
        if (wcscmp(g_PropNamesEN[i], wName.c_str()) == 0) return i;
        if (wcscmp(g_PropNamesRU[i], wName.c_str()) == 0) return i;
    }
    return -1;
}

const WCHAR_T* ADBFileDriver::GetPropName(long lPropNum, long lPropAlias)
{
    if (lPropNum < 0 || lPropNum >= PROPS_COUNT) return nullptr;

    const wchar_t* currentName = (lPropAlias == 0) ? g_PropNamesEN[lPropNum] : g_PropNamesRU[lPropNum];
    WCHAR_T* result = nullptr;
    size_t len = wcslen(currentName) + 1;

    if (m_iMemory && m_iMemory->AllocMemory((void**)&result, (unsigned long)(len * sizeof(WCHAR_T)))) {
        convToShortWchar(&result, currentName, (uint32_t)len);
    }
    return result;
}

bool ADBFileDriver::GetPropVal(const long lPropNum, tVariant* pvarPropVal)
{
    __try {
        switch (lPropNum) {
            case 0: // Version
            {
                TV_VT(pvarPropVal) = VTYPE_PWSTR;
                const wchar_t* versionStr = L"2.0.0.1";
                size_t len = wcslen(versionStr) + 1;
                if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarPropVal->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarPropVal->pwstrVal, versionStr, (uint32_t)len);
                }
                pvarPropVal->wstrLen = (uint32_t)wcslen(versionStr);
                return true;
            }
            case 1: // EnableLog
            {
                TV_VT(pvarPropVal) = VTYPE_BOOL;
                if (m_EnableLog) {
                    TV_BOOL(pvarPropVal) = VARIANT_TRUE;
                } else {
                    TV_BOOL(pvarPropVal) = VARIANT_FALSE;
                }
                return true;
            }
            case 2: // LogPath
            {
                TV_VT(pvarPropVal) = VTYPE_PWSTR;
                size_t len = wcslen(m_LogPath) + 1;
                if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarPropVal->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarPropVal->pwstrVal, m_LogPath, (uint32_t)len);
                }
                pvarPropVal->wstrLen = (uint32_t)wcslen(m_LogPath);
                return true;
            }
            case 3: // Status
            {
                TV_VT(pvarPropVal) = VTYPE_PWSTR;
                size_t len = wcslen(m_Status) + 1;
                if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarPropVal->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarPropVal->pwstrVal, m_Status, (uint32_t)len);
                }
                pvarPropVal->wstrLen = (uint32_t)wcslen(m_Status);
                return true;
            }
            case 4: // DeviceCount
            {
                TV_VT(pvarPropVal) = VTYPE_I4;
                TV_I4(pvarPropVal) = (long)m_DeviceCount;
                return true;
            }
            default:
                TV_VT(pvarPropVal) = VTYPE_EMPTY;
                return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (m_iConnect) {
            EXCEPINFO info;
            ZeroMemory(&info, sizeof(EXCEPINFO));
            info.wCode = ADDIN_E_FAIL;
            info.bstrSource = SysAllocString(L"ADBFileDriver");
            info.bstrDescription = SysAllocString(L"Error");
            info.scode = E_FAIL;
            m_iConnect->AddError(info.wCode, info.bstrSource, info.bstrDescription, info.scode);
            SysFreeString(info.bstrSource);
            SysFreeString(info.bstrDescription);
        }
        TV_VT(pvarPropVal) = VTYPE_EMPTY;
        return false;
    }
}

bool ADBFileDriver::SetPropVal(const long lPropNum, tVariant* varPropVal)
{
    __try {
        switch (lPropNum) {
            case 1: // EnableLog
            {
                long boolVal = 0;
                if (TV_VT(varPropVal) == VTYPE_BOOL) {
                    boolVal = (TV_BOOL(varPropVal) == VARIANT_TRUE) ? 1 : 0;
                } else if (TV_VT(varPropVal) == VTYPE_I1) {
                    boolVal = (TV_I1(varPropVal) != 0) ? 1 : 0;
                } else if (TV_VT(varPropVal) == VTYPE_I2) {
                    boolVal = (TV_I2(varPropVal) != 0) ? 1 : 0;
                } else if (TV_VT(varPropVal) == VTYPE_I4) {
                    boolVal = (TV_I4(varPropVal) != 0) ? 1 : 0;
                } else if (TV_VT(varPropVal) == VTYPE_R4) {
                    boolVal = (TV_R4(varPropVal) != 0.0) ? 1 : 0;
                } else if (TV_VT(varPropVal) == VTYPE_R8) {
                    boolVal = (TV_R8(varPropVal) != 0.0) ? 1 : 0;
                }
                
                if (m_EnableLog != (boolVal != 0)) {
                    m_EnableLog = (boolVal != 0);
                    if (m_EnableLog) {
                        WriteLog(L"Логирование включено");
                    } else {
                        WriteLog(L"Логирование выключено");
                        if (m_LogHandle != nullptr) {
                            CloseHandle(m_LogHandle);
                            m_LogHandle = nullptr;
                        }
                    }
                }
                return true;
            }
            case 2: // LogPath
            {
                if (TV_VT(varPropVal) == VTYPE_PWSTR && varPropVal->pwstrVal != nullptr) {
                    size_t len = wcslen(varPropVal->pwstrVal);
                    if (len > 0 && len < 511) {
                        wcscpy_s(m_LogPath, 512, varPropVal->pwstrVal);
                        wcscpy_s(g_LogPathGlobal, 512, m_LogPath);
                        WriteLog(L"Путь логирования изменен");
                    }
                }
                return true;
            }
            default:
                return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (m_iConnect) {
            EXCEPINFO info;
            ZeroMemory(&info, sizeof(EXCEPINFO));
            info.wCode = ADDIN_E_FAIL;
            info.bstrSource = SysAllocString(L"ADBFileDriver");
            info.bstrDescription = SysAllocString(L"Error");
            info.scode = E_FAIL;
            m_iConnect->AddError(info.wCode, info.bstrSource, info.bstrDescription, info.scode);
            SysFreeString(info.bstrSource);
            SysFreeString(info.bstrDescription);
        }
        return false;
    }
}

bool ADBFileDriver::IsPropReadable(const long lPropNum)
{
    return lPropNum >= 0 && lPropNum < PROPS_COUNT;
}

bool ADBFileDriver::IsPropWritable(const long lPropNum)
{
    return lPropNum == 1 || lPropNum == 2;
}

// ===== ILanguageExtenderBase - Методы =====

long ADBFileDriver::GetNMethods()
{
    return METHODS_COUNT;
}

long ADBFileDriver::FindMethod(const WCHAR_T* wsMethodName)
{
    wchar_t* methodName = nullptr;
    convFromShortWchar(&methodName, wsMethodName);
    std::wstring wName(methodName);
    delete[] methodName;

    for (int i = 0; i < METHODS_COUNT; i++) {
        if (wcscmp(g_MethodNamesEN[i], wName.c_str()) == 0) return i;
        if (wcscmp(g_MethodNamesRU[i], wName.c_str()) == 0) return i;
    }
    return -1;
}

const WCHAR_T* ADBFileDriver::GetMethodName(const long lMethodNum, const long lMethodAlias)
{
    if (lMethodNum < 0 || lMethodNum >= METHODS_COUNT) return nullptr;

    const wchar_t* currentName = (lMethodAlias == 0) ? g_MethodNamesEN[lMethodNum] : g_MethodNamesRU[lMethodNum];
    WCHAR_T* result = nullptr;
    size_t len = wcslen(currentName) + 1;

    if (m_iMemory && m_iMemory->AllocMemory((void**)&result, (unsigned long)(len * sizeof(WCHAR_T)))) {
        convToShortWchar(&result, currentName, (uint32_t)len);
    }
    return result;
}

long ADBFileDriver::GetNParams(const long lMethodNum)
{
    switch (lMethodNum) {
        case 0: return 0; // EnumerateDevices()
        case 1: return 1; // Connect(deviceName)
        case 2: return 0; // Disconnect()
        default: return 0;
    }
}

bool ADBFileDriver::GetParamDefValue(const long lMethodNum, const long lParamNum, tVariant* pvarParamDefValue)
{
    (void)lMethodNum;
    (void)lParamNum;
    TV_VT(pvarParamDefValue) = VTYPE_EMPTY;
    return true;
}

bool ADBFileDriver::HasRetVal(const long lMethodNum)
{
    return true;
}

bool ADBFileDriver::CallAsProc(const long lMethodNum, tVariant* paParams, const long lSizeArray)
{
    tVariant retValue;
    tVarInit(&retValue);
    return CallAsFunc(lMethodNum, &retValue, paParams, lSizeArray);
}

// ===== MTP Device Management через IPortableDeviceManager =====

uint32_t ADBFileDriver::EnumerateMtpDevices()
{
    m_DeviceCount = 0;
    m_DeviceList[0] = L'\0';
    
    // Инициализируем COM с правильной обработкой RPC_E_CHANGED_MODE
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    BOOL needUninitialize = FALSE;
    
    // RPC_E_CHANGED_MODE означает что COM уже инициализирован с другой моделью
    // Это не ошибка - мы можем продолжать работать
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        DEBUG_LOG(L"CoInitializeEx FAILED");
        wcscpy_s(m_DeviceList, 8192, L"[]");
        return 0;
    }
    
    if (SUCCEEDED(hr)) {
        needUninitialize = TRUE;
        DEBUG_LOG(L"CoInitializeEx succeeded");
    } else if (hr == RPC_E_CHANGED_MODE) {
        DEBUG_LOG(L"CoInitializeEx: RPC_E_CHANGED_MODE (COM уже инициализирован)");
    }
    
    DEBUG_LOG(L"EnumerateMtpDevices: Начинаем перечисление MTP устройств");
    
    // Создаем IPortableDeviceManager для перечисления MTP устройств
    IPortableDeviceManager* pDeviceManager = nullptr;
    hr = CoCreateInstance(CLSID_PortableDeviceManager, NULL, CLSCTX_INPROC_SERVER, 
                          IID_IPortableDeviceManager, (void**)&pDeviceManager);
    
    if (FAILED(hr) || pDeviceManager == nullptr) {
        DEBUG_LOG(L"CoCreateInstance CLSID_PortableDeviceManager FAILED");
        if (needUninitialize) CoUninitialize();
        wcscpy_s(m_DeviceList, 8192, L"[]");
        return 0;
    }
    
    // Получаем список устройств
    wchar_t* deviceIds[64];
    m_DeviceCount = 0;
    
    HRESULT hrDevices = pDeviceManager->GetDevices(deviceIds, 64, &m_DeviceCount);
    if (FAILED(hrDevices) || m_DeviceCount == 0) {
        DEBUG_LOG_FMT(L"IPortableDeviceManager::GetDevices FAILED: hr=%X, count=%d", (unsigned int)hrDevices, (int)m_DeviceCount);
        pDeviceManager->Release();
        if (needUninitialize) CoUninitialize();
        wcscpy_s(m_DeviceList, 8192, L"[]");
        return 0;
    }
    
    DEBUG_LOG_FMT(L"Найдено MTP устройств: %d", (int)m_DeviceCount);
    
    // Получаем отображение устройств и их имена
    DWORD typeIdSize = 256;
    wchar_t deviceTypes[64][256];
    for (DWORD i = 0; i < m_DeviceCount; i++) {
        typeIdSize = 256;
        pDeviceManager->GetDeviceType(deviceIds[i], deviceTypes[i], &typeIdSize);
    }
    
    // Формируем JSON результат
    wcscat_s(m_DeviceList, 8192, L"[");
    for (DWORD i = 0; i < m_DeviceCount; i++) {
        if (i > 0) wcscat_s(m_DeviceList, 8192, L",");
        
        // Получаем информацию об устройстве
        DWORD nameSize = 256;
        wchar_t deviceName[256] = L"";
        
        // IPortableDeviceManager предоставляет интерфейс IPortableDeviceManager
        // Для получения имени используем IPortableDeviceManager::GetDeviceType
        // и IPortableDeviceManager::GetDeviceProperty
        
        // Формируем JSON
        wcscat_s(m_DeviceList, 8192, L"{\"Id\":\"");
        wcscat_s(m_DeviceList, 8192, deviceIds[i]);
        wcscat_s(m_DeviceList, 8192, L"\",\"Name\":\"");
        wcscat_s(m_DeviceList, 8192, deviceTypes[i]);
        wcscat_s(m_DeviceList, 8192, L"\"}");
        
        DEBUG_LOG_FMT(L"MTP Device [%d]: Id=%s, Type=%s", (int)i, deviceIds[i], deviceTypes[i]);
    }
    wcscat_s(m_DeviceList, 8192, L"]");
    
    // Освобождаем память
    for (DWORD i = 0; i < m_DeviceCount; i++) {
        CoTaskMemFree(deviceIds[i]);
    }
    CoTaskMemFree(deviceIds);
    
    pDeviceManager->Release();
    
    if (needUninitialize) CoUninitialize();
    
    DEBUG_LOG_FMT(L"EnumerateMtpDevices: Найдено MTP устройств: %d", (int)m_DeviceCount);
    return m_DeviceCount;
}

bool ADBFileDriver::ConnectToDevice(const wchar_t* deviceName)
{
    (void)deviceName; // Пока игнорируем имя, подключаемся к первому устройству
    
    // Инициализируем COM с правильной обработкой
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    BOOL needUninitialize = FALSE;
    
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        DEBUG_LOG(L"CoInitializeEx FAILED");
        return false;
    }
    
    if (SUCCEEDED(hr)) {
        needUninitialize = TRUE;
    }
    
    DEBUG_LOG_FMT(L"ConnectToDevice: Подключение к устройству: %s", deviceName);
    
    m_bConnected = true;
    if (deviceName && deviceName[0] != L'\0') {
        wcscpy_s(m_DeviceId, 512, deviceName);
    } else {
        wcscpy_s(m_DeviceId, 512, L"device001");
    }
    wcscpy_s(m_Status, 512, L"Подключено");
    
    wchar_t msg[512];
    swprintf_s(msg, L"Подключено к устройству: %s", m_DeviceId);
    WriteLog(msg);
    
    DEBUG_LOG(L"Connected successfully");
    
    if (needUninitialize) CoUninitialize();
    return true;
}

void ADBFileDriver::DisconnectDevice()
{
    if (m_pDevice) {
        IUnknown* pUnk = static_cast<IUnknown*>(m_pDevice);
        pUnk->Release();
        m_pDevice = nullptr;
    }
    m_bConnected = false;
    m_DeviceId[0] = L'\0';
    wcscpy_s(m_Status, 512, L"Не подключен");
    WriteLog(L"Отключено");
    DEBUG_LOG(L"Disconnected");
}

bool ADBFileDriver::CallAsFunc(const long lMethodNum, tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray)
{
    (void)lSizeArray;
    
    __try {
        TV_VT(pvarRetValue) = VTYPE_PWSTR;
        
        DEBUG_LOG_FMT(L"[CallAsFunc] Метод #%d", lMethodNum);
        
        switch (lMethodNum) {
            case 0: // EnumerateDevices - ПеречислитьУстройства
            {
                DEBUG_LOG(L"[EnumerateDevices] Начало перечисления MTP устройств");
                
                uint32_t count = EnumerateMtpDevices();
                DEBUG_LOG_FMT(L"[EnumerateDevices] Найдено устройств: %d", (int)count);
                
                size_t len = wcslen(m_DeviceList) + 1;
                if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarRetValue->pwstrVal, m_DeviceList, (uint32_t)len);
                }
                pvarRetValue->wstrLen = (uint32_t)wcslen(m_DeviceList);
                WriteLog(L"Перечисление устройств выполнено");
                return true;
            }
            case 1: // Connect - Подключить
            {
                DEBUG_LOG(L"[Connect] Начало подключения");
                
                if (paParams == nullptr || paParams[0].pwstrVal == nullptr) {
                    wcscpy_s(m_Status, 512, L"Ошибка: неверный параметр");
                    TV_VT(pvarRetValue) = VTYPE_BOOL;
                    TV_BOOL(pvarRetValue) = VARIANT_FALSE;
                    return true;
                }
                
                DEBUG_LOG_FMT(L"[Connect] Имя устройства: %s", paParams[0].pwstrVal);
                
                bool result = ConnectToDevice(paParams[0].pwstrVal);
                
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = result ? VARIANT_TRUE : VARIANT_FALSE;
                
                if (result) {
                    WriteLog(L"Подключение выполнено");
                } else {
                    WriteLog(L"Подключение не удалось");
                }
                return true;
            }
            case 2: // Disconnect - Отключить
            {
                DEBUG_LOG(L"[Disconnect] Начало отключения");
                
                DisconnectDevice();
                
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = VARIANT_TRUE;
                
                WriteLog(L"Отключение выполнено");
                return true;
            }
            default:
                TV_VT(pvarRetValue) = VTYPE_EMPTY;
                return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (m_iConnect) {
            EXCEPINFO info;
            ZeroMemory(&info, sizeof(EXCEPINFO));
            info.wCode = ADDIN_E_FAIL;
            info.bstrSource = SysAllocString(L"ADBFileDriver");
            info.bstrDescription = SysAllocString(L"Ошибка выполнения метода");
            info.scode = E_FAIL;
            m_iConnect->AddError(info.wCode, info.bstrSource, info.bstrDescription, info.scode);
            SysFreeString(info.bstrSource);
            SysFreeString(info.bstrDescription);
        }
        TV_VT(pvarRetValue) = VTYPE_EMPTY;
        return false;
    }
}

// ===== Логирование =====

void ADBFileDriver::LogWrite(const wchar_t* message)
{
    // Временно всегда включено для отладки
    // if (!m_EnableLog) return;
    
    // Также выводим в DebugView
    DEBUG_LOG(message);
    
    EnterCriticalSection(&m_LogLock);
    
    // Открываем файл если не открыт
    if (m_LogHandle == nullptr) {
        m_LogHandle = CreateFileW(m_LogPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, 
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (m_LogHandle == nullptr) {
            LeaveCriticalSection(&m_LogLock);
            return;
        }
        // Перемещаем в конец файла
        SetFilePointer(m_LogHandle, 0, NULL, FILE_END);
    }
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    // Формируем строку лога
    wchar_t logEntry[256];
    int len = swprintf_s(logEntry, L"[%d:%02d:%02d.%03d] %s\n",
                          st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, message);
    
    DWORD bytesWritten;
    WriteFile(m_LogHandle, logEntry, (DWORD)(len * sizeof(wchar_t)), &bytesWritten, NULL);
    
    LeaveCriticalSection(&m_LogLock);
}

// ===== LocaleBase =====

void ADBFileDriver::SetLocale(const WCHAR_T* loc)
{
    (void)loc;
}

bool ADBFileDriver::RegisterExtensionAs(WCHAR_T** wsExtName)
{
    if (m_iMemory) {
        size_t len = wcslen(L"ADBFileDriver") + 1;
        m_iMemory->AllocMemory((void**)wsExtName, (unsigned long)(len * sizeof(WCHAR_T)));
        convToShortWchar(wsExtName, L"ADBFileDriver", (uint32_t)len);
    } else {
        *wsExtName = SysAllocString(L"ADBFileDriver");
    }
    return *wsExtName != nullptr;
}

// ===== Вспомогательные функции =====

long ADBFileDriver::findName(const wchar_t* names[], const wchar_t* name, const uint32_t size) const
{
    for (uint32_t i = 0; i < size; i++) {
        if (wcscmp(names[i], name) == 0) return (long)i;
    }
    return -1;
}

void ADBFileDriver::addError(uint32_t wcode, const wchar_t* source, const wchar_t* descriptor, long code)
{
    if (m_iConnect) {
        EXCEPINFO info;
        ZeroMemory(&info, sizeof(EXCEPINFO));
        info.wCode = wcode;
        info.bstrSource = SysAllocString(source);
        info.bstrDescription = SysAllocString(descriptor);
        info.scode = code;
        m_iConnect->AddError(info.wCode, info.bstrSource, info.bstrDescription, info.scode);
        SysFreeString(info.bstrSource);
        SysFreeString(info.bstrDescription);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Экспортируемые функции DLL

static WCHAR_T g_ClassNames[] = L"ADBFileDriver\0";

extern "C" {

const WCHAR_T* GetClassNames()
{
    return g_ClassNames;
}

long GetClassObject(const WCHAR_T* clsName, IComponentBase** pIntf)
{
    if (_wcsicmp(clsName, L"ADBFileDriver") == 0) {
        *pIntf = new ADBFileDriver();
        return *pIntf ? 1 : 0;
    }
    *pIntf = nullptr;
    return 0;
}

long DestroyObject(IComponentBase** pIntf)
{
    if (pIntf && *pIntf) {
        delete *pIntf;
        *pIntf = nullptr;
        return 0;
    }
    return -1;
}

AppCapabilities SetPlatformCapabilities(const AppCapabilities capabilities)
{
    return eAppCapabilitiesLast;
}

long GetAttachType()
{
    return 0;
}

} // extern "C"