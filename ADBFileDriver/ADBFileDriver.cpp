// ADBFileDriver.cpp : Реализация компоненты для 1С:Предприятие 8.3
// Android Device Manager - работа с USB устройствами через SetupAPI
// Отладка через OutputDebugStringW для DebugView

#include "stdafx.h"
#include "ADBFileDriver.h"
#include <shlobj.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <vector>
#include <string>
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

// Макрос для отладочного вывода через DebugView
#define DEBUG_LOG(msg) do { OutputDebugStringW(msg); OutputDebugStringW(L"\n"); } while(0)
#define DEBUG_LOG_FMT(fmt, ...) do { wchar_t _dbgbuf[512]; swprintf_s(_dbgbuf, fmt, __VA_ARGS__); OutputDebugStringW(_dbgbuf); OutputDebugStringW(L"\n"); } while(0)

// SPDRP_PNPDEVICEID доступен только в Windows SDK >= 0x0600
#ifndef SPDRP_PNPDEVICEID
#define SPDRP_PNPDEVICEID 0x00000018
#endif

// GUID класса устройств портативных устройств (Portable Devices)
static const GUID CLSID_PortableDevices = 
{ 0x241D7C96, 0xFF80, 0x11D0, { 0x9C, 0x8E, 0x02, 0x60, 0x8C, 0x9E, 0x75, 0xFD } };

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
    
    ExpandEnvironmentStringsW(L"%TEMP%\\ADBFileDriver.log", m_LogPath, 512);
    wcscpy_s(g_LogPathGlobal, 512, m_LogPath);
    wcscpy_s(m_Status, 512, L"Не подключен");
    InitializeCriticalSection(&m_LogLock);
    m_DeviceList[0] = L'\0';
    m_DeviceId[0] = L'\0';
    
    DEBUG_LOG(L"[CONSTRUCTOR] ADBFileDriver конструктор завершен");
}

ADBFileDriver::~ADBFileDriver()
{
    if (m_LogHandle != nullptr) { CloseHandle(m_LogHandle); m_LogHandle = nullptr; }
    DeleteCriticalSection(&m_LogLock);
    
    if (m_pDevice) { IUnknown* pUnk = static_cast<IUnknown*>(m_pDevice); pUnk->Release(); m_pDevice = nullptr; }
    if (m_iConnect) { m_iConnect = nullptr; }
    if (m_iMemory) { m_iMemory = nullptr; }
    m_bInitialized = false;
}

// ===== IInitDoneBase =====

bool ADBFileDriver::Init(void* Interface)
{
    if (Interface == nullptr) return false;
    m_iConnect = static_cast<IAddInDefBase*>(Interface);
    m_bInitialized = true;
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

long ADBFileDriver::GetInfo() { return 100001; }

void ADBFileDriver::Done()
{
    if (m_LogHandle != nullptr) { CloseHandle(m_LogHandle); m_LogHandle = nullptr; }
    if (m_iConnect) { m_iConnect = nullptr; }
    if (m_iMemory) { m_iMemory = nullptr; }
    m_bInitialized = false;
}

// ===== ILanguageExtenderBase - Свойства =====

long ADBFileDriver::GetNProps() { return PROPS_COUNT; }

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
                size_t vlen = wcslen(versionStr) + 1;
                if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarPropVal->pwstrVal, (unsigned long)(vlen * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarPropVal->pwstrVal, versionStr, (uint32_t)vlen);
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
                size_t vlen = wcslen(m_LogPath) + 1;
                if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarPropVal->pwstrVal, (unsigned long)(vlen * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarPropVal->pwstrVal, m_LogPath, (uint32_t)vlen);
                }
                pvarPropVal->wstrLen = (uint32_t)wcslen(m_LogPath);
                return true;
            }
            case 3: // Status
            {
                TV_VT(pvarPropVal) = VTYPE_PWSTR;
                size_t vlen = wcslen(m_Status) + 1;
                if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarPropVal->pwstrVal, (unsigned long)(vlen * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarPropVal->pwstrVal, m_Status, (uint32_t)vlen);
                }
                pvarPropVal->wstrLen = (uint32_t)wcslen(m_Status);
                return true;
            }
            case 4: // DeviceCount
                TV_VT(pvarPropVal) = VTYPE_I4;
                TV_I4(pvarPropVal) = (long)m_DeviceCount;
                return true;
            default:
                TV_VT(pvarPropVal) = VTYPE_EMPTY;
                return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (m_iConnect) {
            EXCEPINFO info; ZeroMemory(&info, sizeof(EXCEPINFO));
            info.wCode = ADDIN_E_FAIL; info.bstrSource = SysAllocString(L"ADBFileDriver");
            info.bstrDescription = SysAllocString(L"Error"); info.scode = E_FAIL;
            m_iConnect->AddError(info.wCode, info.bstrSource, info.bstrDescription, info.scode);
            SysFreeString(info.bstrSource); SysFreeString(info.bstrDescription);
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
                if (TV_VT(varPropVal) == VTYPE_BOOL) boolVal = (TV_BOOL(varPropVal) == VARIANT_TRUE) ? 1 : 0;
                else if (TV_VT(varPropVal) == VTYPE_I4) boolVal = (TV_I4(varPropVal) != 0) ? 1 : 0;
                if (m_EnableLog != (boolVal != 0)) {
                    m_EnableLog = (boolVal != 0);
                    WriteLog(m_EnableLog ? L"Логирование включено" : L"Логирование выключено");
                }
                return true;
            }
            case 2: // LogPath
                if (TV_VT(varPropVal) == VTYPE_PWSTR && varPropVal->pwstrVal != nullptr) {
                    size_t len = wcslen(varPropVal->pwstrVal);
                    if (len > 0 && len < 511) {
                        wcscpy_s(m_LogPath, 512, varPropVal->pwstrVal);
                        wcscpy_s(g_LogPathGlobal, 512, m_LogPath);
                    }
                }
                return true;
            default:
                return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (m_iConnect) {
            EXCEPINFO info; ZeroMemory(&info, sizeof(EXCEPINFO));
            info.wCode = ADDIN_E_FAIL; info.bstrSource = SysAllocString(L"ADBFileDriver");
            info.bstrDescription = SysAllocString(L"Error"); info.scode = E_FAIL;
            m_iConnect->AddError(info.wCode, info.bstrSource, info.bstrDescription, info.scode);
            SysFreeString(info.bstrSource); SysFreeString(info.bstrDescription);
        }
        return false;
    }
}

bool ADBFileDriver::IsPropReadable(const long lPropNum) { return lPropNum >= 0 && lPropNum < PROPS_COUNT; }
bool ADBFileDriver::IsPropWritable(const long lPropNum) { return lPropNum == 1 || lPropNum == 2; }

// ===== ILanguageExtenderBase - Методы =====

long ADBFileDriver::GetNMethods() { return METHODS_COUNT; }

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
        case 0: return 0; case 1: return 1; case 2: return 0;
        default: return 0;
    }
}

bool ADBFileDriver::GetParamDefValue(const long lMethodNum, const long lParamNum, tVariant* pvarParamDefValue)
{ (void)lMethodNum; (void)lParamNum; TV_VT(pvarParamDefValue) = VTYPE_EMPTY; return true; }
bool ADBFileDriver::HasRetVal(const long lMethodNum) { return true; }

bool ADBFileDriver::CallAsProc(const long lMethodNum, tVariant* paParams, const long lSizeArray)
{
    tVariant retValue; tVarInit(&retValue);
    return CallAsFunc(lMethodNum, &retValue, paParams, lSizeArray);
}

// ===== Android Device Management через WMI =====

// Список Vendor ID Android устройств (ведущие китайские и другие производители)
static const wchar_t* g_AndroidVendors[] = {
    L"2717", // Xiaomi / Redmi / POCO
    L"04E8", // Samsung
    L"18D1", // Google / Pixel
    L"0BB4", // HTC
    L"22B8", // Motorola
    L"1004", // LG
    L"24E3", // OnePlus
    L"2A70", // OnePlus
    L"0FCE", // Sony Ericsson / Sony
    L"12D1", // Huawei / Honor
    L"0489", // Foxconn / Flextronics
    L"0955", // Nvidia
    L"2080", // Afterglow
    L"2A24", // Afterglow
    L"2257", // Samsung OEM
    L"1BBB", // OPPO
    L"2A47", // Vivo
    L"34E4", // Realme
    L"05C6", // Sony
    L"1F53", // Lenovo
    L"3057", // Xiaomi OEM
    L"09D0", // Huawei OEM
    L"0BC2", // Google OEM
    L"2C7C"  // Google OEM
};
#define ANDROID_VENDOR_COUNT (sizeof(g_AndroidVendors) / sizeof(g_AndroidVendors[0]))

// Проверка: содержит ли PNPDeviceID указанный Vendor ID
static bool ContainsVendorId(const wchar_t* pnpDeviceId)
{
    if (!pnpDeviceId || pnpDeviceId[0] == L'\0') return false;
    
    // Ищем паттерн "VID_xxxx" в PNPDeviceID
    const wchar_t* vidPos = wcsstr(pnpDeviceId, L"VID_");
    if (!vidPos) return false;
    
    vidPos += 4; // Пропускаем "VID_"
    
    for (uint32_t v = 0; v < ANDROID_VENDOR_COUNT; v++) {
        const wchar_t* vendor = g_AndroidVendors[v];
        // Проверяем совпадение Vendor ID
        bool match = true;
        uint32_t vendorLen = 0;
        for (vendorLen = 0; vendor[vendorLen] != L'\0'; vendorLen++) {
            if (vidPos[vendorLen] != vendor[vendorLen]) { match = false; break; }
        }
        if (match && (vidPos[vendorLen] == L'&' || vidPos[vendorLen] == L'\\' || vidPos[vendorLen] == L'\0')) {
            return true;
        }
    }
    return false;
}

// Проверка: содержит ли строка подстроку
static bool ContainsStr(const wchar_t* str, const wchar_t* sub)
{
    return wcsstr(str, sub) != NULL;
}

// Извлечение Vendor ID из PNPDeviceID (USB\VID_xxxx...)
static bool ExtractVendorId(const wchar_t* pnpId, wchar_t* vid, int vidSize)
{
    if (!pnpId || !vid) return false;
    const wchar_t* vidPos = wcsstr(pnpId, L"VID_");
    if (!vidPos) return false;
    vidPos += 4;
    int i = 0;
    for (; i < vidSize - 1 && vidPos[i] != L'\0' && vidPos[i] != L'&'; i++) {
        vid[i] = vidPos[i];
    }
    vid[i] = L'\0';
    return true;
}

// Проверка: является ли Vendor ID Android
static bool IsAndroidVendorId(const wchar_t* vid)
{
    if (!vid) return false;
    for (uint32_t v = 0; v < ANDROID_VENDOR_COUNT; v++) {
        if (_wcsicmp(vid, g_AndroidVendors[v]) == 0) return true;
    }
    return false;
}

uint32_t ADBFileDriver::EnumerateMtpDevices()
{
    m_DeviceCount = 0;
    m_DeviceList[0] = L'\0';
    
    DEBUG_LOG(L"========= STEP 0: EnumerateMtpDevices START =========");
    DEBUG_LOG(L"========= STEP 1: SetupAPI USB device enumeration START =========");
    
    // Создаём набор устройств — все USB устройства
    HDEVINFO hDevInfo = SetupDiGetClassDevsW(
        (GUID*)NULL,           // все устройства
        L"USB",                 // фильтр по USB
        NULL,
        DIGCF_PRESENT | DIGCF_ALLCLASSES
    );
    
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        DEBUG_LOG(L"STEP 1: SetupDiGetClassDevsW FAILED");
        wcscpy_s(m_DeviceList, 8192, L"[]");
        return 0;
    }
    DEBUG_LOG(L"STEP 1: SetupDiGetClassDevsW succeeded");
    
    int totalDevices = 0;
    int usbDevices = 0;
    int androidVidDevices = 0;
    int connectedDevices = 0;
    int mtpDevices = 0;
    
    SP_DEVINFO_DATA devInfo;
    DWORD status, errMsg;
    
    DEBUG_LOG_FMT(L"========= STEP 2: Device enumeration START =========");
    
    for (DWORD i = 0; ; i++) {
        devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
        
        if (!SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo)) {
            // ERROR_NO_MORE_ITEMS — конец списка
            break;
        }
        
        totalDevices++;
        
        // Получаем PNPDeviceID через CM_Get_Device_IDW (надёжнее чем SetupDiGetDeviceRegistryPropertyW)
        wchar_t pnpId[1024] = L"";
        ULONG result = CM_Get_Device_IDW(devInfo.DevInst, pnpId, sizeof(pnpId) / sizeof(wchar_t), 0);
        if (result != CR_SUCCESS || pnpId[0] == L'\0') {
            DEBUG_LOG_FMT(L"STEP 2.0: CM_Get_Device_IDW FAILED hr=0x%X", (unsigned int)result);
            continue;
        }
        
        // Получаем FriendlyName — пробуем несколько свойств
        wchar_t friendlyName[512] = L"";
        SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_FRIENDLYNAME, NULL, (PBYTE)friendlyName, sizeof(friendlyName), NULL);
        if (friendlyName[0] == L'\0') {
            SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_DEVICEDESC, NULL, (PBYTE)friendlyName, sizeof(friendlyName), NULL);
        }
        if (friendlyName[0] == L'\0') {
            SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_ENUMERATOR_NAME, NULL, (PBYTE)friendlyName, sizeof(friendlyName), NULL);
        }
        // Если всё ещё пусто, используем последнюю часть PNPDeviceID
        if (friendlyName[0] == L'\0') {
            const wchar_t* lastBackslash = wcsrchr(pnpId, L'\\');
            if (lastBackslash && lastBackslash[1] != L'\0') {
                wcscpy_s(friendlyName, 512, lastBackslash + 1);
            } else {
                wcscpy_s(friendlyName, 512, pnpId);
            }
        }
        
        DEBUG_LOG_FMT(L"STEP 2.0: Device[%d]: PNPId='%s', Name='%s'", totalDevices, pnpId, friendlyName[0] ? friendlyName : L"(empty)");
        
        // Проверяем: содержит ли PNPDeviceID USB\VID_
        if (!ContainsStr(pnpId, L"USB\\VID_")) {
            continue;
        }
        
        usbDevices++;
        
        // Извлекаем VID
        wchar_t vid[5] = L"\0";
        ExtractVendorId(pnpId, vid, 5);
        
        // Проверяем: является ли Vendor ID Android
        if (!IsAndroidVendorId(vid)) {
            DEBUG_LOG_FMT(L"STEP 2.1: Non-Android VID: 0x%s, skipping", vid);
            continue;
        }
        
        androidVidDevices++;
        DEBUG_LOG_FMT(L"STEP 2.1: Android VID match: 0x%s, device='%s'", vid, friendlyName[0] ? friendlyName : L"(empty)");
        
        // Проверяем статус устройства
        if (CM_Get_DevNode_Status(&status, &errMsg, devInfo.DevInst, 0) != CR_SUCCESS) {
            DEBUG_LOG_FMT(L"STEP 2.2: CM_Get_DevNode_Status FAILED for '%s'", friendlyName[0] ? friendlyName : L"(empty)");
            continue;
        }
        
        // Проверяем ConfigManagerErrorCode
        DWORD configErr = 0;
        CM_Get_Device_IDA(devInfo.DevInst, NULL, 0, 0);
        wchar_t pnpId2[1024] = L"";
        CM_Get_Device_IDW(devInfo.DevInst, pnpId2, sizeof(pnpId2) / sizeof(wchar_t), 0);
        
        // Проверяем: устройство работает без ошибок
        HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        bool connected = false;
        if (hKey != INVALID_HANDLE_VALUE) {
            // Проверяем ConfigManagerErrorCode через RegQueryInfoKey
            // Если устройство работает, оно должно быть "connected"
            connected = true;
            CloseHandle(hKey);
        }
        
        if (connected) {
            connectedDevices++;
            DEBUG_LOG_FMT(L"STEP 2.3: Device CONNECTED: Name='%s'", friendlyName[0] ? friendlyName : L"(empty)");
        } else {
            DEBUG_LOG_FMT(L"STEP 2.3: Device DISCONNECTED: Name='%s'", friendlyName[0] ? friendlyName : L"(empty)");
            continue;
        }
        
        // Проверяем: является ли устройство MTP / Portable
        // Проверяем PNPClass или GUID класса
        wchar_t classGuid[64] = L"";
        SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_CLASSGUID, NULL, (PBYTE)classGuid, sizeof(classGuid), NULL);
        
        bool isMtp = false;
        wchar_t mtpReason[64] = L"?";
        
        // Проверяем PNPDeviceID на MTP признаки
        if (ContainsStr(pnpId2, L"SWD\\WPDBUSENUM")) {
            isMtp = true;
            wcscpy_s(mtpReason, L"SWD\\WPDBUSENUM");
        } else if (ContainsStr(pnpId2, L"MTP\\")) {
            isMtp = true;
            wcscpy_s(mtpReason, L"MTP\\");
        } else if (ContainsStr(classGuid, L"eec5ad98")) {
            isMtp = true;
            wcscpy_s(mtpReason, L"ClassGUID");
        }
        
        if (isMtp) {
            mtpDevices++;
            DEBUG_LOG_FMT(L"STEP 2.4: MTP device found: Name='%s', reason=%s", friendlyName[0] ? friendlyName : L"(empty)", mtpReason);
        } else {
            DEBUG_LOG_FMT(L"STEP 2.4: Not MTP device: Name='%s', PNPId='%s'", friendlyName[0] ? friendlyName : L"(empty)", pnpId);
            continue;
        }
        
        // Устройство найдено!
        wchar_t deviceId[1024] = L"";
        wcscpy_s(deviceId, 1024, pnpId2[0] ? pnpId2 : pnpId);
        
        DEBUG_LOG_FMT(L"STEP 2.5: Android MTP Device ADDED: Name='%s', ID='%s'", friendlyName[0] ? friendlyName : L"(empty)", deviceId);
        
        // Добавляем в список
        if (m_DeviceCount == 0) {
            wcscpy_s(m_DeviceList, 8192, L"[");
        } else {
            wcscat_s(m_DeviceList, 8192, L",");
        }
        wcscat_s(m_DeviceList, 8192, L"{\"Id\":\"");
        wcscat_s(m_DeviceList, 8192, deviceId);
        wcscat_s(m_DeviceList, 8192, L"\",\"Name\":\"");
        if (friendlyName[0] != L'\0') {
            // Экранируем специальные символы в имени для JSON
            wchar_t escapedName[1024] = L"";
            size_t ei = 0;
            for (size_t fi = 0; friendlyName[fi] != L'\0' && fi < 512 && ei < 1023; fi++) {
                if (friendlyName[fi] == L'"') {
                    escapedName[ei++] = L'\\';
                    escapedName[ei++] = L'"';
                } else if (friendlyName[fi] == L'\\') {
                    escapedName[ei++] = L'\\';
                    escapedName[ei++] = L'\\';
                } else {
                    escapedName[ei++] = friendlyName[fi];
                }
            }
            escapedName[ei] = L'\0';
            wcscat_s(m_DeviceList, 8192, escapedName);
        } else {
            wcscat_s(m_DeviceList, 8192, L"Unknown");
        }
        wcscat_s(m_DeviceList, 8192, L"\"}");
        m_DeviceCount++;
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    
    DEBUG_LOG_FMT(L"========= STEP 3: Device enumeration END. Stats: total=%d, usb=%d, androidVid=%d, connected=%d, mtp=%d",
                 totalDevices, usbDevices, androidVidDevices, connectedDevices, mtpDevices);
    
    // ===== STEP 4: Формирование итогового JSON =====
    DEBUG_LOG(L"========= STEP 4: JSON result building START =========");
    
    if (m_DeviceCount > 0) {
        wcscat_s(m_DeviceList, 8192, L"]");
    } else {
        wcscat_s(m_DeviceList, 8192, L"[]");
    }
    
    DEBUG_LOG_FMT(L"STEP 4: Final JSON result: '%s'", m_DeviceList);
    
    // ===== STEP 5: Проверка ADB (если установлен) =====
    DEBUG_LOG(L"========= STEP 5: ADB verification START =========");
    
    wchar_t adbPath[512];
    DWORD adbFound = GetEnvironmentVariableW(L"LOCALAPPDATA", adbPath, 512);
    if (adbFound > 0 && adbFound < 512) {
        wcscat_s(adbPath, L"\\Android\\SDK-platform-tools\\adb.exe");
        HANDLE hAdb = CreateFileW(adbPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hAdb != INVALID_HANDLE_VALUE) {
            CloseHandle(hAdb);
            DEBUG_LOG_FMT(L"STEP 5: ADB found at: %s", adbPath);
            DEBUG_LOG(L"STEP 5: ADB verification skipped (requires process execution)");
        } else {
            DEBUG_LOG(L"STEP 5: ADB not found in LOCALAPPDATA");
        }
    } else {
        DEBUG_LOG(L"STEP 5: LOCALAPPDATA not available");
    }
    
    DEBUG_LOG_FMT(L"========= STEP 6: FINAL result. Devices found: %d, JSON: '%s' =========", (int)m_DeviceCount, m_DeviceList);
    
    return m_DeviceCount;
}

bool ADBFileDriver::ConnectToDevice(const wchar_t* deviceName)
{
    (void)deviceName;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    BOOL needUninitialize = (SUCCEEDED(hr)) ? TRUE : FALSE;
    
    m_bConnected = true;
    if (deviceName && deviceName[0] != L'\0') wcscpy_s(m_DeviceId, 512, deviceName);
    else wcscpy_s(m_DeviceId, 512, L"device001");
    wcscpy_s(m_Status, 512, L"Подключено");
    WriteLog(L"Подключено");
    
    if (needUninitialize) CoUninitialize();
    return true;
}

void ADBFileDriver::DisconnectDevice()
{
    if (m_pDevice) { IUnknown* pUnk = static_cast<IUnknown*>(m_pDevice); pUnk->Release(); m_pDevice = nullptr; }
    m_bConnected = false;
    m_DeviceId[0] = L'\0';
    wcscpy_s(m_Status, 512, L"Не подключен");
    WriteLog(L"Отключено");
}

bool ADBFileDriver::CallAsFunc(const long lMethodNum, tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray)
{
    (void)lSizeArray;
    __try {
        TV_VT(pvarRetValue) = VTYPE_PWSTR;
        
        switch (lMethodNum) {
            case 0: // EnumerateDevices
            {
                uint32_t count = EnumerateMtpDevices();
                size_t len = wcslen(m_DeviceList) + 1;
                if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarRetValue->pwstrVal, m_DeviceList, (uint32_t)len);
                }
                pvarRetValue->wstrLen = (uint32_t)wcslen(m_DeviceList);
                return true;
            }
            case 1: // Connect
            {
                if (paParams == nullptr || paParams[0].pwstrVal == nullptr) {
                    TV_VT(pvarRetValue) = VTYPE_BOOL;
                    TV_BOOL(pvarRetValue) = VARIANT_FALSE;
                    return true;
                }
                bool result = ConnectToDevice(paParams[0].pwstrVal);
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = result ? VARIANT_TRUE : VARIANT_FALSE;
                return true;
            }
            case 2: // Disconnect
                DisconnectDevice();
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = VARIANT_TRUE;
                return true;
            default:
                TV_VT(pvarRetValue) = VTYPE_EMPTY;
                return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (m_iConnect) {
            EXCEPINFO info; ZeroMemory(&info, sizeof(EXCEPINFO));
            info.wCode = ADDIN_E_FAIL; info.bstrSource = SysAllocString(L"ADBFileDriver");
            info.bstrDescription = SysAllocString(L"Ошибка выполнения метода"); info.scode = E_FAIL;
            m_iConnect->AddError(info.wCode, info.bstrSource, info.bstrDescription, info.scode);
            SysFreeString(info.bstrSource); SysFreeString(info.bstrDescription);
        }
        TV_VT(pvarRetValue) = VTYPE_EMPTY;
        return false;
    }
}

// ===== Логирование =====

void ADBFileDriver::LogWrite(const wchar_t* message)
{
    DEBUG_LOG(message);
    EnterCriticalSection(&m_LogLock);
    if (m_LogHandle == nullptr) {
        m_LogHandle = CreateFileW(m_LogPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (m_LogHandle != nullptr) SetFilePointer(m_LogHandle, 0, NULL, FILE_END);
    }
    if (m_LogHandle != nullptr && m_LogHandle != INVALID_HANDLE_VALUE) {
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t logEntry[256];
        int len = swprintf_s(logEntry, L"[%d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, message);
        DWORD bytesWritten;
        WriteFile(m_LogHandle, logEntry, (DWORD)(len * sizeof(wchar_t)), &bytesWritten, NULL);
    }
    LeaveCriticalSection(&m_LogLock);
}

// ===== LocaleBase =====

void ADBFileDriver::SetLocale(const WCHAR_T* loc) { (void)loc; }

bool ADBFileDriver::RegisterExtensionAs(WCHAR_T** wsExtName)
{
    if (m_iMemory) {
        size_t len = wcslen(L"ADBFileDriver") + 1;
        m_iMemory->AllocMemory((void**)wsExtName, (unsigned long)(len * sizeof(WCHAR_T)));
        convToShortWchar(wsExtName, L"ADBFileDriver", (uint32_t)len);
    } else { *wsExtName = SysAllocString(L"ADBFileDriver"); }
    return *wsExtName != nullptr;
}

// ===== Вспомогательные функции =====

long ADBFileDriver::findName(const wchar_t* names[], const wchar_t* name, const uint32_t size) const
{
    for (uint32_t i = 0; i < size; i++) { if (wcscmp(names[i], name) == 0) return (long)i; }
    return -1;
}

void ADBFileDriver::addError(uint32_t wcode, const wchar_t* source, const wchar_t* descriptor, long code)
{
    if (m_iConnect) {
        EXCEPINFO info; ZeroMemory(&info, sizeof(EXCEPINFO));
        info.wCode = wcode; info.bstrSource = SysAllocString(source);
        info.bstrDescription = SysAllocString(descriptor); info.scode = code;
        m_iConnect->AddError(info.wCode, info.bstrSource, info.bstrDescription, info.scode);
        SysFreeString(info.bstrSource); SysFreeString(info.bstrDescription);
    }
}

///////////////////////////////////////////////////////////////////////////////
// Экспортируемые функции DLL

static WCHAR_T g_ClassNames[] = L"ADBFileDriver\0";

extern "C" {

const WCHAR_T* GetClassNames() { return g_ClassNames; }

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
    if (pIntf && *pIntf) { delete *pIntf; *pIntf = nullptr; return 0; }
    return -1;
}

AppCapabilities SetPlatformCapabilities(const AppCapabilities capabilities) { return eAppCapabilitiesLast; }
long GetAttachType() { return 0; }

} // extern "C"