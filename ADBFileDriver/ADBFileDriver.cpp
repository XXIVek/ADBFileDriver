// ADBFileDriver.cpp : Реализация компоненты для 1С:Предприятие 8.3
// Прямой USB доступ через AdbWinUsbApi.dll

#include "stdafx.h"
#include "ADBFileDriver.h"

// Глобальные массивы имен свойств
static const wchar_t* g_PropNamesEN[] = { L"Version", L"EnableLog", L"LogPath" };
static const wchar_t* g_PropNamesRU[] = { L"Версия", L"ВключитьЛогирование", L"ПутьДляФайлаЛогирования" };
#define PROPS_COUNT 3

// Глобальные массивы имен методов (пусто для MVP)
#define METHODS_COUNT 0

///////////////////////////////////////////////////////////////////////////////
// ADBFileDriver

ADBFileDriver::ADBFileDriver(void)
    : m_iConnect(nullptr), m_iMemory(nullptr), m_bInitialized(false)
    , m_EnableLog(false)
{
    // Инициализация пути логирования по умолчанию (временная папка)
    ExpandEnvironmentStringsW(L"%TEMP%\\ADBFileDriver.log", m_LogPath, 512);
}

ADBFileDriver::~ADBFileDriver()
{
    Done();
}

// ===== IInitDoneBase =====

bool ADBFileDriver::Init(void* Interface)
{
    if (Interface == nullptr) return false;
    m_iConnect = static_cast<IAddInDefBase*>(Interface);
    m_bInitialized = true;
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
    // Версия: 1.0.0.1 = 100001
    return 100001;
}

void ADBFileDriver::Done()
{
    // Закрытие лог файла
    {
        std::lock_guard<std::mutex> lock(m_LogMutex);
        if (m_LogFile.is_open()) {
            m_LogFile.close();
        }
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
    return PROPS_COUNT; // Версия, ВключитьЛогирование, ПутьДляФайлаЛогирования
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
    try {
        switch (lPropNum) {
            case 0: // Version
                TV_VT(pvarPropVal) = VTYPE_PWSTR;
                // Выделить память через менеджер памяти 1С
                if (m_iMemory) {
                    size_t len = wcslen(L"1.0.0.1") + 1;
                    m_iMemory->AllocMemory((void**)&pvarPropVal->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)));
                    convToShortWchar(&pvarPropVal->pwstrVal, L"1.0.0.1", (uint32_t)len);
                } else {
                    pvarPropVal->pwstrVal = reinterpret_cast<WCHAR_T*>(SysAllocString(L"1.0.0.1"));
                }
                return pvarPropVal->pwstrVal != nullptr;
            default:
                TV_VT(pvarPropVal) = VTYPE_EMPTY;
                return false;
        }
    } catch (...) {
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
    // Свойства только для чтения
    (void)lPropNum;
    (void)varPropVal;
    return false;
}

bool ADBFileDriver::IsPropReadable(const long lPropNum)
{
    return lPropNum >= 0 && lPropNum < PROPS_COUNT;
}

bool ADBFileDriver::IsPropWritable(const long lPropNum)
{
    return false;
}

// ===== ILanguageExtenderBase - Методы =====

long ADBFileDriver::GetNMethods()
{
    return METHODS_COUNT; // Нет методов в MVP
}

// ===== Логирование =====

void ADBFileDriver::LogWrite(const wchar_t* message)
{
    if (!m_EnableLog) return;
    
    std::lock_guard<std::mutex> lock(m_LogMutex);
    
    // Открываем файл если не открыт
    if (!m_LogFile.is_open()) {
        m_LogFile.open(m_LogPath, std::ios::app);
        if (!m_LogFile.is_open()) return;
    }
    
    // Добавляем временную метку
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    m_LogFile << "[" 
              << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "." 
              << st.wMilliseconds << "] " 
              << message << std::endl;
    m_LogFile.flush();
}

long ADBFileDriver::FindMethod(const WCHAR_T* wsMethodName)
{
    (void)wsMethodName;
    return -1; // Методов нет
}

const WCHAR_T* ADBFileDriver::GetMethodName(const long lMethodNum, const long lMethodAlias)
{
    (void)lMethodNum;
    (void)lMethodAlias;
    return nullptr; // Методов нет
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
    return false;
}

bool ADBFileDriver::CallAsProc(const long lMethodNum, tVariant* paParams, const long lSizeArray)
{
    (void)lMethodNum;
    (void)paParams;
    (void)lSizeArray;
    return false;
}

bool ADBFileDriver::CallAsFunc(const long lMethodNum, tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray)
{
    (void)lMethodNum;
    (void)pvarRetValue;
    (void)paParams;
    (void)lSizeArray;
    return false;
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
    return 0; // eCanAttachAny
}

} // extern "C"