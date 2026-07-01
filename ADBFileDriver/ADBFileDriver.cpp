// ADBFileDriver.cpp : Реализация компоненты для 1С:Предприятие 8.3
// Прямой USB доступ через AdbWinUsbApi.dll

#include "stdafx.h"
#include "ADBFileDriver.h"

// Глобальные массивы имен свойств
static const wchar_t* g_PropNamesEN[] = { L"Version", L"EnableLog", L"LogPath", L"Status" };
static const wchar_t* g_PropNamesRU[] = { L"Версия", L"ВключитьЛогирование", L"ПутьДляФайлаЛогирования", L"Статус" };
#define PROPS_COUNT 4

// Глобальные массивы имен методов
static const wchar_t* g_MethodNamesEN[] = { L"Connect", L"Disconnect" };
static const wchar_t* g_MethodNamesRU[] = { L"Подключить", L"Отключить" };
#define METHODS_COUNT 2

///////////////////////////////////////////////////////////////////////////////
// ADBFileDriver

ADBFileDriver::ADBFileDriver(void)
    : m_iConnect(nullptr), m_iMemory(nullptr), m_bInitialized(false)
    , m_EnableLog(false), m_bConnected(false)
    , m_LogHandle(INVALID_HANDLE_VALUE)
{
    // Инициализация пути логирования по умолчанию (временная папка)
    ExpandEnvironmentStringsW(L"%TEMP%\\ADBFileDriver.log", m_LogPath, 512);
    // Инициализация статуса
    wcscpy_s(m_Status, 512, L"Не подключен");
    // Инициализация критической секции
    InitializeCriticalSection(&m_LogLock);
}

ADBFileDriver::~ADBFileDriver()
{
    // Безопасное уничтожение - используем только Windows API
    if (m_LogHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_LogHandle);
        m_LogHandle = INVALID_HANDLE_VALUE;
    }
    DeleteCriticalSection(&m_LogLock);
    
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
    if (Interface == nullptr) return false;
    m_iConnect = static_cast<IAddInDefBase*>(Interface);
    m_bInitialized = true;
    LogWrite(L"Компонента инициализирована");
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
    if (m_LogHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_LogHandle);
        m_LogHandle = INVALID_HANDLE_VALUE;
    }
    
    if (m_iConnect) {
        m_iConnect = nullptr;
    }
    if (m_iMemory) {
        m_iMemory = nullptr;
    }
    m_bInitialized = false;
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
                const wchar_t* versionStr = L"1.0.0.1";
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
                TV_BOOL(pvarPropVal) = m_EnableLog ? VARIANT_TRUE : VARIANT_FALSE;
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
                if (TV_VT(varPropVal) == VTYPE_BOOL) {
                    m_EnableLog = (TV_BOOL(varPropVal) == VARIANT_TRUE);
                    if (m_EnableLog) {
                        LogWrite(L"Логирование включено");
                    } else {
                        LogWrite(L"Логирование выключено");
                        if (m_LogHandle != INVALID_HANDLE_VALUE) {
                            CloseHandle(m_LogHandle);
                            m_LogHandle = INVALID_HANDLE_VALUE;
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
                        LogWrite(L"Путь логирования изменен");
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
    (void)lMethodNum;
    return 0;
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
    (void)lMethodNum;
    return true;
}

bool ADBFileDriver::CallAsProc(const long lMethodNum, tVariant* paParams, const long lSizeArray)
{
    tVariant retValue;
    tVarInit(&retValue);
    return CallAsFunc(lMethodNum, &retValue, paParams, lSizeArray);
}

bool ADBFileDriver::CallAsFunc(const long lMethodNum, tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray)
{
    (void)paParams;
    (void)lSizeArray;
    
    __try {
        TV_VT(pvarRetValue) = VTYPE_BOOL;
        
        switch (lMethodNum) {
            case 0: // Connect - Подключить
            {
                wcscpy_s(m_Status, 512, L"Подключено");
                m_bConnected = true;
                LogWrite(L"Подключение выполнено");
                TV_BOOL(pvarRetValue) = VARIANT_TRUE;
                return true;
            }
            case 1: // Disconnect - Отключить
            {
                if (m_bConnected) {
                    wcscpy_s(m_Status, 512, L"Не подключен");
                    m_bConnected = false;
                    LogWrite(L"Отключение выполнено");
                }
                TV_BOOL(pvarRetValue) = VARIANT_TRUE;
                return true;
            }
            default:
                TV_BOOL(pvarRetValue) = VARIANT_FALSE;
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
        TV_VT(pvarRetValue) = VTYPE_BOOL;
        TV_BOOL(pvarRetValue) = VARIANT_FALSE;
        return false;
    }
}

// ===== Логирование =====

void ADBFileDriver::LogWrite(const wchar_t* message)
{
    if (!m_EnableLog) return;
    
    EnterCriticalSection(&m_LogLock);
    
    // Открываем файл если не открыт
    if (m_LogHandle == INVALID_HANDLE_VALUE) {
        m_LogHandle = CreateFileW(m_LogPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, 
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (m_LogHandle == INVALID_HANDLE_VALUE) {
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