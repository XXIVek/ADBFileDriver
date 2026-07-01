// ADBFileDriver.cpp : Реализация компоненты для 1С:Предприятие 8.3
// Прямой USB доступ через AdbWinUsbApi.dll

#include "stdafx.h"
#include "ADBFileDriver.h"
#include "adb_api.h"

// Для AdbWinUsbApi.dll
typedef ADBAPIHANDLE (__cdecl *AdbEnumInterfaces_t)(GUID, bool, bool, bool);
typedef bool (__cdecl *AdbNextInterface_t)(ADBAPIHANDLE, AdbInterfaceInfo*, unsigned long*);
typedef bool (__cdecl *AdbResetInterfaceEnum_t)(ADBAPIHANDLE);
typedef ADBAPIHANDLE (__cdecl *AdbCreateInterfaceByName_t)(const wchar_t*);
typedef bool (__cdecl *AdbGetSerialNumber_t)(ADBAPIHANDLE, void*, unsigned long*, bool);
typedef bool (__cdecl *AdbGetUsbDeviceDescriptor_t)(ADBAPIHANDLE, USB_DEVICE_DESCRIPTOR*);
typedef bool (__cdecl *AdbCloseHandle_t)(ADBAPIHANDLE);

// GUID для ADB устройств: {F72FE0D4-CBCB-407d-8814-9ED673D0DD6B}
static const GUID ADB_USB_CLASS_ID = 
{ 0xf72fe0d4, 0xcbcb, 0x407d, { 0x88, 0x14, 0x9e, 0xd6, 0x73, 0xd0, 0xdd, 0x6b } };

// Глобальные массивы имен свойств
static const wchar_t* g_PropNamesEN[] = { L"Version", L"EnableLog", L"LogPath", L"Status", L"DeviceCount" };
static const wchar_t* g_PropNamesRU[] = { L"Версия", L"ВключитьЛогирование", L"ПутьДляФайлаЛогирования", L"Статус", L"КоличествоУстройств" };
#define PROPS_COUNT 5

// Глобальные массивы имен методов
static const wchar_t* g_MethodNamesEN[] = { L"Connect", L"Disconnect", L"EnumerateDevices", L"GetDeviceInfo" };
static const wchar_t* g_MethodNamesRU[] = { L"Подключить", L"Отключить", L"ПеречислитьУстройства", L"ПолучитьИнформациюОУстройстве" };
#define METHODS_COUNT 4

// Загрузки функций AdbWinUsbApi
static HMODULE g_AdbWinUsbHandle = nullptr;
static AdbEnumInterfaces_t pAdbEnumInterfaces = nullptr;
static AdbNextInterface_t pAdbNextInterface = nullptr;
static AdbResetInterfaceEnum_t pAdbResetInterfaceEnum = nullptr;
static AdbCreateInterfaceByName_t pAdbCreateInterfaceByName = nullptr;
static AdbGetSerialNumber_t pAdbGetSerialNumber = nullptr;
static AdbGetUsbDeviceDescriptor_t pAdbGetUsbDeviceDescriptor = nullptr;
static AdbCloseHandle_t pAdbCloseHandle = nullptr;

static bool LoadAdbFunctions()
{
    if (g_AdbWinUsbHandle != nullptr) return true;
    
    // Пробуем загрузить из нескольких мест
    const wchar_t* searchPaths[] = {
        L"AdbWinUsbApi.dll",                                    // System32/SysWOW64
        NULL
    };
    
    for (int i = 0; i < 2; i++) {
        g_AdbWinUsbHandle = LoadLibraryW(searchPaths[i]);
        if (g_AdbWinUsbHandle != nullptr) {
            goto load_functions;
        }
    }
    
    // Пробуем из папки adb в текущей директории
    {
        wchar_t currentDir[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, currentDir);
        wchar_t path[MAX_PATH];
        wchar_t* backslash = wcsrchr(currentDir, L'\\');
        if (backslash) {
            wcscpy_s(backslash + 1, MAX_PATH - (backslash - currentDir), L"adb\\AdbWinUsbApi.dll");
            wcscpy_s(path, MAX_PATH, currentDir);
            backslash = wcsrchr(path, L'\\');
            if (backslash) {
                wcscpy_s(backslash + 1, MAX_PATH - (backslash - path), L"adb\\AdbWinUsbApi.dll");
                g_AdbWinUsbHandle = LoadLibraryW(path);
                if (g_AdbWinUsbHandle != nullptr) {
                    goto load_functions;
                }
            }
        }
    }
    
    return false;
    
load_functions:
    
    pAdbEnumInterfaces = (AdbEnumInterfaces_t)GetProcAddress(g_AdbWinUsbHandle, "AdbEnumInterfaces");
    pAdbNextInterface = (AdbNextInterface_t)GetProcAddress(g_AdbWinUsbHandle, "AdbNextInterface");
    pAdbResetInterfaceEnum = (AdbResetInterfaceEnum_t)GetProcAddress(g_AdbWinUsbHandle, "AdbResetInterfaceEnum");
    pAdbCreateInterfaceByName = (AdbCreateInterfaceByName_t)GetProcAddress(g_AdbWinUsbHandle, "AdbCreateInterfaceByName");
    pAdbGetSerialNumber = (AdbGetSerialNumber_t)GetProcAddress(g_AdbWinUsbHandle, "AdbGetSerialNumber");
    pAdbGetUsbDeviceDescriptor = (AdbGetUsbDeviceDescriptor_t)GetProcAddress(g_AdbWinUsbHandle, "AdbGetUsbDeviceDescriptor");
    pAdbCloseHandle = (AdbCloseHandle_t)GetProcAddress(g_AdbWinUsbHandle, "AdbCloseHandle");
    
    return (pAdbEnumInterfaces && pAdbNextInterface && pAdbCloseHandle);
}

///////////////////////////////////////////////////////////////////////////////
// ADBFileDriver

ADBFileDriver::ADBFileDriver(void)
    : m_iConnect(nullptr), m_iMemory(nullptr), m_bInitialized(false)
    , m_EnableLog(false), m_bConnected(false)
    , m_LogHandle(INVALID_HANDLE_VALUE)
    , m_DeviceCount(0)
{
    // Инициализация пути логирования по умолчанию (временная папка)
    ExpandEnvironmentStringsW(L"%TEMP%\\ADBFileDriver.log", m_LogPath, 512);
    // Инициализация статуса
    wcscpy_s(m_Status, 512, L"Не подключен");
    // Инициализация критической секции
    InitializeCriticalSection(&m_LogLock);
    // Инициализация списка устройств
    m_DeviceList[0] = L'\0';
}

ADBFileDriver::~ADBFileDriver()
{
    // Безопасное уничтожение - используем только Windows API
    if (m_LogHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_LogHandle);
        m_LogHandle = INVALID_HANDLE_VALUE;
    }
    DeleteCriticalSection(&m_LogLock);
    
    if (g_AdbWinUsbHandle != nullptr) {
        FreeLibrary(g_AdbWinUsbHandle);
        g_AdbWinUsbHandle = nullptr;
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
    switch (lMethodNum) {
        case 0: return 0; // Connect()
        case 1: return 0; // Disconnect()
        case 2: return 0; // EnumerateDevices()
        case 3: return 1; // GetDeviceInfo(index)
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

// ===== Вспомогательная функция: форматирование GUID =====
static void GuidToString(const GUID* guid, wchar_t* buffer, size_t bufferSize)
{
    swprintf_s(buffer, (uint32_t)bufferSize, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
               guid->Data1, guid->Data2, guid->Data3,
               guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
               guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
}

// ===== Перечисление USB устройств =====
static uint32_t EnumerateAdbDevices(wchar_t* jsonBuffer, uint32_t bufferSize)
{
    if (!pAdbEnumInterfaces || !pAdbNextInterface || !pAdbCloseHandle) return 0;
    
    ADBAPIHANDLE enumHandle = pAdbEnumInterfaces(ADB_USB_CLASS_ID, true, true, true);
    if (enumHandle == nullptr) return 0;
    
    uint32_t count = 0;
    unsigned long infoSize = 0;
    
    // Сначала определяем размер буфера
    if (pAdbNextInterface(enumHandle, nullptr, &infoSize)) {
        AdbInterfaceInfo* info = (AdbInterfaceInfo*)malloc(infoSize);
        if (info) {
            if (pAdbNextInterface(enumHandle, info, &infoSize)) {
                // Найдено хотя бы одно устройство
                ADBAPIHANDLE iface = pAdbCreateInterfaceByName(info->device_name);
                if (iface) {
                    wchar_t serial[256] = L"";
                    unsigned long serialSize = 256;
                    if (pAdbGetSerialNumber(iface, serial, &serialSize, false)) {
                        // Копируем serial
                        wcsncpy_s(serial, 256, serial, 255);
                    }
                    
                    USB_DEVICE_DESCRIPTOR devDesc;
                    ZeroMemory(&devDesc, sizeof(devDesc));
                    if (pAdbGetUsbDeviceDescriptor(iface, &devDesc)) {
                        // Формируем JSON для одного устройства
                        if (count > 0 && jsonBuffer) {
                            wcscat_s(jsonBuffer, bufferSize, L",");
                        }
                        if (jsonBuffer) {
                            wchar_t vidStr[16], pidStr[16];
                            swprintf_s(vidStr, 16, L"0x%04X", devDesc.idVendor);
                            swprintf_s(pidStr, 16, L"0x%04X", devDesc.idProduct);
                            wcscat_s(jsonBuffer, bufferSize, L"[");
                            wcscat_s(jsonBuffer, bufferSize, L"\"serial\":\"");
                            wcscat_s(jsonBuffer, bufferSize, serial);
                            wcscat_s(jsonBuffer, bufferSize, L"\",\"vendorId\":");
                            wcscat_s(jsonBuffer, bufferSize, vidStr);
                            wcscat_s(jsonBuffer, bufferSize, L",\"productId\":");
                            wcscat_s(jsonBuffer, bufferSize, pidStr);
                            wcscat_s(jsonBuffer, bufferSize, L"]");
                        }
                    }
                    pAdbCloseHandle(iface);
                }
                count++;
            }
            free(info);
        }
    }
    
    pAdbCloseHandle(enumHandle);
    return count;
}

bool ADBFileDriver::CallAsFunc(const long lMethodNum, tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray)
{
    (void)lSizeArray;
    
    __try {
        TV_VT(pvarRetValue) = VTYPE_PWSTR;
        
        switch (lMethodNum) {
            case 0: // Connect - Подключить
            {
                wcscpy_s(m_Status, 512, L"Подключено");
                m_bConnected = true;
                LogWrite(L"Подключение выполнено");
                TV_VT(pvarRetValue) = VTYPE_BOOL;
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
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = VARIANT_TRUE;
                return true;
            }
            case 2: // EnumerateDevices - ПеречислитьУстройства
            {
                if (!LoadAdbFunctions()) {
                    wcscpy_s(m_Status, 512, L"Ошибка: AdbWinUsbApi.dll не найден");
                    LogWrite(L"Ошибка: AdbWinUsbApi.dll не найден");
                    TV_VT(pvarRetValue) = VTYPE_BOOL;
                    TV_BOOL(pvarRetValue) = VARIANT_FALSE;
                    return true;
                }
                
                // Сначала определяем размер
                m_DeviceList[0] = L'\0';
                uint32_t requiredSize = 4; // "[]" + null
                
                // Перечисляем устройства
                ADBAPIHANDLE enumHandle = pAdbEnumInterfaces(ADB_USB_CLASS_ID, true, true, true);
                if (enumHandle == nullptr) {
                    wcscpy_s(m_DeviceList, L"[]");
                    m_DeviceCount = 0;
                    TV_VT(pvarRetValue) = VTYPE_PWSTR;
                    size_t len = wcslen(m_DeviceList) + 1;
                    if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                        convToShortWchar((WCHAR_T**)&pvarRetValue->pwstrVal, m_DeviceList, (uint32_t)len);
                    }
                    pvarRetValue->wstrLen = (uint32_t)wcslen(m_DeviceList);
                    return true;
                }
                
                // Собираем устройства
                wchar_t deviceSerials[16][64]; // serial для каждого устройства
                unsigned short deviceVendorIds[16];
                unsigned short deviceProductIds[16];
                uint32_t deviceCount = 0;
                unsigned long infoSize = 0;
                
                // Определяем количество устройств
                if (pAdbNextInterface(enumHandle, nullptr, &infoSize)) {
                    AdbInterfaceInfo* info = (AdbInterfaceInfo*)malloc(infoSize);
                    if (info) {
                        while (pAdbNextInterface(enumHandle, info, &infoSize)) {
                            ADBAPIHANDLE iface = pAdbCreateInterfaceByName(info->device_name);
                            if (iface) {
                                wchar_t serial[64] = L"";
                                unsigned long serialSize = 64;
                                if (pAdbGetSerialNumber(iface, serial, &serialSize, false)) {
                                    wcsncpy_s(deviceSerials[deviceCount], 64, serial, 63);
                                    
                                    USB_DEVICE_DESCRIPTOR devDesc;
                                    ZeroMemory(&devDesc, sizeof(devDesc));
                                    if (pAdbGetUsbDeviceDescriptor(iface, &devDesc)) {
                                        deviceVendorIds[deviceCount] = devDesc.idVendor;
                                        deviceProductIds[deviceCount] = devDesc.idProduct;
                                    }
                                    deviceCount++;
                                }
                                pAdbCloseHandle(iface);
                            }
                            if (deviceCount >= 16) break;
                        }
                        free(info);
                    }
                }
                pAdbCloseHandle(enumHandle);
                
                m_DeviceCount = deviceCount;
                
                // Формируем JSON
                if (deviceCount == 0) {
                    wcscpy_s(m_DeviceList, L"[]");
                } else {
                    wcscpy_s(m_DeviceList, L"[");
                    for (uint32_t i = 0; i < deviceCount; i++) {
                        if (i > 0) { wcscat_s(m_DeviceList, L","); }
                        wcscat_s(m_DeviceList, L"[");
                        wcscat_s(m_DeviceList, L"\"serial\":\"");
                        wcscat_s(m_DeviceList, deviceSerials[i]);
                        wcscat_s(m_DeviceList, L"\",\"vendorId\":0x");
                        wchar_t vidStr[16];
                        swprintf_s(vidStr, L"%04X", (unsigned int)deviceVendorIds[i]);
                        wcscat_s(m_DeviceList, vidStr);
                        wcscat_s(m_DeviceList, L",\"productId\":0x");
                        wchar_t pidStr[16];
                        swprintf_s(pidStr, L"%04X", (unsigned int)deviceProductIds[i]);
                        wcscat_s(m_DeviceList, pidStr);
                        wcscat_s(m_DeviceList, L"]");
                    }
                    wcscat_s(m_DeviceList, L"]");
                }
                
                size_t len = wcslen(m_DeviceList) + 1;
                if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarRetValue->pwstrVal, m_DeviceList, (uint32_t)len);
                }
                pvarRetValue->wstrLen = (uint32_t)wcslen(m_DeviceList);
                LogWrite(L"Перечисление устройств выполнено");
                return true;
            }
            case 3: // GetDeviceInfo - ПолучитьИнформациюОУстройстве
            {
                if (lMethodNum != 3 || paParams == nullptr) {
                    TV_VT(pvarRetValue) = VTYPE_EMPTY;
                    return false;
                }
                
                // Получаем индекс устройства
                long index = 0;
                if (TV_VT(&paParams[0]) == VTYPE_I4) {
                    index = TV_I4(&paParams[0]);
                }
                
                if (index < 0 || index >= (long)m_DeviceCount) {
                    wcscpy_s(m_DeviceList, L"");
                    TV_VT(pvarRetValue) = VTYPE_PWSTR;
                    pvarRetValue->wstrLen = 0;
                    return true;
                }
                
                // Возвращаем информацию о конкретном устройстве
                // В MVP возвращаем JSON со всем списком
                size_t len = wcslen(m_DeviceList) + 1;
                if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarRetValue->pwstrVal, m_DeviceList, (uint32_t)len);
                }
                pvarRetValue->wstrLen = (uint32_t)wcslen(m_DeviceList);
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