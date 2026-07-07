// ADBFileDriver.cpp : Реализация компоненты для 1С:Предприятие 8.3
// Android Device Manager - работа с USB устройствами через ADB
// Отладка через OutputDebugStringW для DebugView

#include "stdafx.h"
#include "ADBFileDriver.h"
#include <shlobj.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <shlwapi.h>
#include <vector>
#include <string>
#include <cstdarg>
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")

// Макрос для отладочного вывода через DebugView
#define DEBUG_LOG(msg) do { OutputDebugStringW(msg); OutputDebugStringW(L"\n"); } while(0)
#define DEBUG_LOG_FMT(fmt, ...) do { wchar_t _dbgbuf[512]; swprintf_s(_dbgbuf, fmt, __VA_ARGS__); OutputDebugStringW(_dbgbuf); OutputDebugStringW(L"\n"); } while(0)

// SPDRP_PNPDEVICEID
#ifndef SPDRP_PNPDEVICEID
#define SPDRP_PNPDEVICEID 0x00000018
#endif

// Глобальные массивы имен свойств
static const wchar_t* g_PropNamesEN[] = { L"Version", L"EnableLog", L"LogPath", L"Status", L"DeviceCount", L"FileCount", L"CurrentCatalog", L"AdbDirectory" };
static const wchar_t* g_PropNamesRU[] = { L"Версия", L"ВключитьЛогирование", L"ПутьДляФайлаЛогирования", L"Статус", L"КоличествоУстройств", L"КоличествоФайлов", L"ТекущийКаталог", L"КаталогADB" };
#define PROPS_COUNT 8

// Глобальные массивы имен методов
static const wchar_t* g_MethodNamesEN[] = { 
    L"EnumerateDevices", L"Connect", L"Disconnect", L"ListFiles", 
    L"DownloadFile", L"UploadFile", L"DeleteFile", L"ListNames",
    L"ListFilesRecursive" 
};
static const wchar_t* g_MethodNamesRU[] = { 
    L"ПеречислитьУстройства", L"Подключить", L"Отключить", L"СписокФайлов", 
    L"СкачатьФайл", L"ЗагрузитьФайл", L"УдалитьФайл", L"СписокИменФайлов",
    L"СписокФайловРекурсивно"
};
#define METHODS_COUNT 9

// ===== Глобальные переменные =====
static wchar_t g_adbPath[512] = L"";
static wchar_t g_DllDirectory[1024] = L""; // Каталог DLL (для FindAdbExe)
static wchar_t g_LogPathGlobal[512] = L"";
static bool g_LogEnabled = false;

// ===== Forward declarations =====
static bool FindAdbExe();
static bool AdbExec(const wchar_t* args, wchar_t* output, DWORD outputSize);
static void DebugLogW(const wchar_t* msg);
static void DebugLogFmtW(const wchar_t* fmt, ...);
static bool AdbShellList(const wchar_t* serial, const wchar_t* remotePath, wchar_t* fileList, DWORD fileListSize);

static void WriteLog(const wchar_t* msg)
{
    if (!g_LogEnabled || g_LogPathGlobal[0] == L'\0') return;
    
    wchar_t logPath[512];
    DWORD pathLen = ExpandEnvironmentStringsW(g_LogPathGlobal, logPath, 512);
    if (pathLen == 0 || pathLen > 512) return;
    
    HANDLE hLog = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, 
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hLog == INVALID_HANDLE_VALUE) return;
    
    DWORD dwPos = SetFilePointer(hLog, 0, NULL, FILE_CURRENT);
    if (dwPos == 0 || dwPos == 0xFFFFFFFF) {
        BYTE bom[3] = { 0xEF, 0xBB, 0xBF };
        WriteFile(hLog, bom, 3, NULL, NULL);
    }
    SetFilePointer(hLog, 0, NULL, FILE_END);
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    wchar_t timeBuf[32];
    swprintf_s(timeBuf, L"[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, msg, -1, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) { CloseHandle(hLog); return; }
    
    char* utf8Str = new char[utf8Len + 64];
    WideCharToMultiByte(CP_UTF8, 0, msg, -1, utf8Str, utf8Len, NULL, NULL);
    
    char finalEntry[1024];
    int timeLen = WideCharToMultiByte(CP_ACP, 0, timeBuf, -1, finalEntry, 32, NULL, NULL);
    sprintf_s(finalEntry + timeLen, 1024 - timeLen, "%s\r\n", utf8Str);
    
    DWORD bytesWritten;
    WriteFile(hLog, finalEntry, (DWORD)strlen(finalEntry), &bytesWritten, NULL);
    
    delete[] utf8Str;
    CloseHandle(hLog);
}

///////////////////////////////////////////////////////////////////////////////
// ADBFileDriver

ADBFileDriver::ADBFileDriver(void)
    : m_iConnect(nullptr), m_iMemory(nullptr), m_bInitialized(false)
    , m_EnableLog(false), m_bConnected(false)
    , m_LogHandle(nullptr)
    , m_DeviceCount(0)
{
    ExpandEnvironmentStringsW(L"%TEMP%\\ADBFileDriver.log", m_LogPath, 512);
    wcscpy_s(g_LogPathGlobal, 512, m_LogPath);
    wcscpy_s(m_Status, 512, L"Не подключен");
    InitializeCriticalSection(&m_LogLock);
    m_DeviceList[0] = L'\0';
    m_FileList[0] = L'\0';
    m_DeviceId[0] = L'\0';
    m_AdbSerial[0] = L'\0';
    m_CurrentPath[0] = L'\0';
    m_DeviceCount = 0;
    m_FileCount = 0;
    
    // m_AdbDirectory инициализируется пользователем через свойство КаталогADB
    m_AdbDirectory[0] = L'\0';
}

ADBFileDriver::~ADBFileDriver()
{
    if (m_LogHandle != nullptr && m_LogHandle != INVALID_HANDLE_VALUE) { CloseHandle((HANDLE)m_LogHandle); m_LogHandle = nullptr; }
    DeleteCriticalSection(&m_LogLock);
    
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

long ADBFileDriver::GetInfo() { return 21000; }

void ADBFileDriver::Done()
{
    if (m_LogHandle != nullptr && m_LogHandle != INVALID_HANDLE_VALUE) { CloseHandle((HANDLE)m_LogHandle); m_LogHandle = nullptr; }
    if (m_iConnect) { m_iConnect = nullptr; }
    if (m_iMemory) { m_iMemory = nullptr; }
    m_bInitialized = false;
    
    DebugLogW(L"[DONE] ADBFileDriver Done()");
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
    // Инициализируем возвращаемое значение
    tVarInit(pvarPropVal);
    
    __try {
        switch (lPropNum) {
            case 0: {
                const wchar_t* versionStr = L"2.10.0.0";
                pvarPropVal->pwstrVal = nullptr;
                pvarPropVal->wstrLen = 0;
                size_t vlen = wcslen(versionStr) + 1;
                if (vlen > 1 && m_iMemory && m_iMemory->AllocMemory((void**)&pvarPropVal->pwstrVal, (unsigned long)(vlen * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarPropVal->pwstrVal, versionStr, (uint32_t)vlen);
                    pvarPropVal->wstrLen = (uint32_t)wcslen(versionStr);
                }
                TV_VT(pvarPropVal) = VTYPE_PWSTR;
                return true;
            }
            case 1: {
                TV_VT(pvarPropVal) = VTYPE_BOOL;
                TV_BOOL(pvarPropVal) = m_EnableLog ? VARIANT_TRUE : VARIANT_FALSE;
                return true;
            }
            case 2: {
                pvarPropVal->pwstrVal = nullptr;
                pvarPropVal->wstrLen = 0;
                size_t vlen = wcslen(m_LogPath) + 1;
                if (vlen > 1 && m_iMemory && m_iMemory->AllocMemory((void**)&pvarPropVal->pwstrVal, (unsigned long)(vlen * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarPropVal->pwstrVal, m_LogPath, (uint32_t)vlen);
                    pvarPropVal->wstrLen = (uint32_t)wcslen(m_LogPath);
                }
                TV_VT(pvarPropVal) = VTYPE_PWSTR;
                return true;
            }
            case 3: {
                pvarPropVal->pwstrVal = nullptr;
                pvarPropVal->wstrLen = 0;
                size_t vlen = wcslen(m_Status) + 1;
                if (vlen > 1 && m_iMemory && m_iMemory->AllocMemory((void**)&pvarPropVal->pwstrVal, (unsigned long)(vlen * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarPropVal->pwstrVal, m_Status, (uint32_t)vlen);
                    pvarPropVal->wstrLen = (uint32_t)wcslen(m_Status);
                }
                TV_VT(pvarPropVal) = VTYPE_PWSTR;
                return true;
            }
            case 4:
                TV_VT(pvarPropVal) = VTYPE_I4;
                TV_I4(pvarPropVal) = (long)m_DeviceCount;
                return true;
            case 5:
                TV_VT(pvarPropVal) = VTYPE_I4;
                TV_I4(pvarPropVal) = (long)m_FileCount;
                return true;
            case 6: {
                pvarPropVal->pwstrVal = nullptr;
                pvarPropVal->wstrLen = 0;
                size_t vlen = wcslen(m_CurrentPath) + 1;
                if (vlen > 1 && m_iMemory && m_iMemory->AllocMemory((void**)&pvarPropVal->pwstrVal, (unsigned long)(vlen * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarPropVal->pwstrVal, m_CurrentPath, (uint32_t)vlen);
                    pvarPropVal->wstrLen = (uint32_t)wcslen(m_CurrentPath);
                }
                TV_VT(pvarPropVal) = VTYPE_PWSTR;
                return true;
            }
            case 7: { // epAdbDirectory
                pvarPropVal->pwstrVal = nullptr;
                pvarPropVal->wstrLen = 0;
                size_t vlen = wcslen(m_AdbDirectory) + 1;
                if (vlen > 1 && m_iMemory && m_iMemory->AllocMemory((void**)&pvarPropVal->pwstrVal, (unsigned long)(vlen * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarPropVal->pwstrVal, m_AdbDirectory, (uint32_t)vlen);
                    pvarPropVal->wstrLen = (uint32_t)wcslen(m_AdbDirectory);
                }
                TV_VT(pvarPropVal) = VTYPE_PWSTR;
                return true;
            }
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
            case 1: {
                long boolVal = 0;
                if (TV_VT(varPropVal) == VTYPE_BOOL) boolVal = (TV_BOOL(varPropVal) != 0) ? 1 : 0;
                else if (TV_VT(varPropVal) == VTYPE_I4) boolVal = (TV_I4(varPropVal) != 0) ? 1 : 0;
                if (m_EnableLog != (boolVal != 0)) {
                    m_EnableLog = (boolVal != 0);
                    wcscpy_s(g_LogPathGlobal, 512, m_LogPath);
                    g_LogEnabled = m_EnableLog;
                }
                return true;
            }
            case 2:
                if (TV_VT(varPropVal) == VTYPE_PWSTR && varPropVal->pwstrVal != nullptr) {
                    size_t len = wcslen(varPropVal->pwstrVal);
                    if (len > 0 && len < 511) {
                        wcscpy_s(m_LogPath, 512, varPropVal->pwstrVal);
                        wcscpy_s(g_LogPathGlobal, 512, m_LogPath);
                        g_LogEnabled = m_EnableLog;
                    }
                }
                return true;
            case 7: // epAdbDirectory
                // КаталоADB содержит полный путь к adb.exe
                // Просто копируем и добавляем \ в конце если нет
                if (TV_VT(varPropVal) == VTYPE_PWSTR && varPropVal->pwstrVal != nullptr) {
                    size_t len = wcslen(varPropVal->pwstrVal);
                    if (len > 0 && len < 1024) {
                        // Копируем путь как есть
                        wcscpy_s(m_AdbDirectory, 1024, varPropVal->pwstrVal);
                        
                        // Добавляем обратный слэш если его нет
                        if (wcslen(m_AdbDirectory) > 0 && m_AdbDirectory[wcslen(m_AdbDirectory) - 1] != L'\\')
                            wcscat_s(m_AdbDirectory, 1024, L"\\");
                        
                        // Также обновляем g_DllDirectory для совместимости с FindAdbExe
                        wcscpy_s(g_DllDirectory, 1024, varPropVal->pwstrVal);
                        if (wcslen(g_DllDirectory) > 0 && g_DllDirectory[wcslen(g_DllDirectory) - 1] != L'\\')
                            wcscat_s(g_DllDirectory, 1024, L"\\");
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
bool ADBFileDriver::IsPropWritable(const long lPropNum) { return lPropNum == 1 || lPropNum == 2 || lPropNum == 7; } // 7 = epAdbDirectory

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
    case 0: return 0; case 1: return 1; case 2: return 0; case 3: return 1;
    case 4: return 2; case 5: return 2; case 6: return 1; case 7: return 1; case 8: return 1;
    default: return 0;
    }
}

bool ADBFileDriver::GetParamDefValue(const long, const long, tVariant* pvarParamDefValue) { TV_VT(pvarParamDefValue) = VTYPE_EMPTY; return true; }
bool ADBFileDriver::HasRetVal(const long) { return true; }

bool ADBFileDriver::CallAsProc(const long lMethodNum, tVariant* paParams, const long lSizeArray)
{
    tVariant retValue; tVarInit(&retValue);
    return CallAsFunc(lMethodNum, &retValue, paParams, lSizeArray);
}

// ===== Android Vendor IDs =====
static const wchar_t* g_AndroidVendors[] = {
    L"2717", L"04E8", L"18D1", L"0BB4", L"22B8", L"1004", L"24E3", L"2A70",
    L"0FCE", L"12D1", L"0489", L"0955", L"2080", L"2A24", L"2257", L"1BBB",
    L"2A47", L"34E4", L"05C6", L"1F53", L"3057", L"09D0", L"0BC2", L"2C7C"
};
#define ANDROID_VENDOR_COUNT (sizeof(g_AndroidVendors) / sizeof(g_AndroidVendors[0]))

static bool IsAndroidVendorId(const wchar_t* pnpId)
{
    if (!pnpId) return false;
    const wchar_t* vidPos = wcsstr(pnpId, L"VID_");
    if (!vidPos) return false;
    vidPos += 4;
    for (uint32_t v = 0; v < ANDROID_VENDOR_COUNT; v++) {
        const wchar_t* vendor = g_AndroidVendors[v];
        bool match = true;
        for (uint32_t i = 0; vendor[i] != L'\0'; i++) {
            if (vidPos[i] != vendor[i]) { match = false; break; }
        }
        if (match && (vidPos[4] == L'&' || vidPos[4] == L'\\' || vidPos[4] == L'\0')) return true;
    }
    return false;
}

// ===== ADB helper functions =====

static bool FindAdbExe()
{
    wchar_t dbgMsg[512];
    
    OutputDebugStringW(L"[FINDADB] ENTER");
    if (g_adbPath[0] != L'\0') { OutputDebugStringW(L"[FINDADB] Already set, returning true"); return true; }
    
    // STEP 1: Поиск относительно каталога DLL (из g_DllDirectory)
    OutputDebugStringW(L"[FINDADB] STEP 1: Check g_DllDirectory");
    
    wchar_t dllPath[1024] = L"";
    if (g_DllDirectory[0] != L'\0') {
        wcscpy_s(dllPath, 1024, g_DllDirectory);
    } else {
        OutputDebugStringW(L"[FINDADB]   g_DllDirectory is empty");
    }
    
    if (dllPath[0] != L'\0') {
        swprintf_s(dbgMsg, L"[FINDADB] ADB dir: %s", dllPath);
        OutputDebugStringW(dbgMsg);
        
        // Проверяем adb.exe непосредственно в каталоге
        wchar_t adbPathFull[1024];
        swprintf_s(adbPathFull, 1024, L"%sadb.exe", dllPath);
        swprintf_s(dbgMsg, L"[FINDADB]   Check: %s", adbPathFull);
        OutputDebugStringW(dbgMsg);
        if (PathFileExistsW(adbPathFull)) {
            wcscpy_s(g_adbPath, 512, adbPathFull);
            swprintf_s(dbgMsg, L"[FINDADB] FOUND: %s", adbPathFull);
            OutputDebugStringW(dbgMsg);
            return true;
        } else {
            swprintf_s(dbgMsg, L"[FINDADB]   Result: NOT FOUND");
            OutputDebugStringW(dbgMsg);
        }
    }
    
    OutputDebugStringW(L"[FINDADB] STEP 2: Check PATH env");
    wchar_t pathEnv[1024];
    DWORD ret = GetEnvironmentVariableW(L"PATH", pathEnv, 1024);
    swprintf_s(dbgMsg, L"[FINDADB] GetEnvironmentVariableW returned: %d", ret);
    OutputDebugStringW(dbgMsg);
    
    if (ret > 0 && ret < 1024) {
        wchar_t* copy = new (std::nothrow) wchar_t[1024];
        if (copy) {
            wcscpy_s(copy, 1024, pathEnv);
            wchar_t* tok = wcstok_s(copy, L";;", nullptr);
            int tokCount = 0;
            while (tok) {
                swprintf_s(dbgMsg, L"[FINDADB]   PATH[%d]: %s", tokCount, tok);
                OutputDebugStringW(dbgMsg);
                wchar_t full[1024]; swprintf_s(full, 1024, L"%s\\\\adb.exe", tok);
                if (PathFileExistsW(full)) {
                    wcscpy_s(g_adbPath, 512, full);
                    swprintf_s(dbgMsg, L"[FINDADB] FOUND in PATH: %s", full);
                    OutputDebugStringW(dbgMsg);
                    delete[] copy;
                    return true;
                }
                tok = wcstok_s(nullptr, L";;", nullptr);
                tokCount++;
            }
            delete[] copy;
        }
    }
    
    OutputDebugStringW(L"[FINDADB] NOT FOUND, returning false");
    return false;
}

// ===== ADB Execute без таймаута =====
// ADB сам запускает сервер при первой команде если он не запущен
static bool AdbExec(const wchar_t* args, wchar_t* output, DWORD outputSize)
{
    if (!FindAdbExe()) {
        DebugLogW(L"[ADB] AdbExec: FindAdbExe failed");
        return false;
    }
    
    // Создаём pipe для перехвата stdout
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        DebugLogW(L"[ADB] CreatePipe hRead/hWrite failed");
        return false;
    }
    
    // Создаём pipe для перехвата stderr
    HANDLE hErrRead, hErrWrite;
    if (!CreatePipe(&hErrRead, &hErrWrite, &sa, 0)) {
        CloseHandle(hRead); CloseHandle(hWrite); 
        DebugLogW(L"[ADB] CreatePipe hErrRead/hErrWrite failed");
        return false;
    }
    
    // Создаём null-device для stdin (чтобы adb не ждал ввода)
    HANDLE hStdInput = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hStdInput == INVALID_HANDLE_VALUE || hStdInput == NULL) {
        CloseHandle(hRead); CloseHandle(hWrite); CloseHandle(hErrRead); CloseHandle(hErrWrite); 
        DebugLogW(L"[ADB] CreateFile NUL failed");
        return false;
    }
    
    // ADB возвращает ANSI/UTF-8 вывод, используем ANSI API
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {0};
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hErrWrite;
    si.hStdInput = hStdInput;
    
    // Формируем полную команду: "C:\path\to\adb.exe" args
    char adbPathA[1024];
    int pathLen = WideCharToMultiByte(CP_ACP, 0, g_adbPath, -1, adbPathA, sizeof(adbPathA), nullptr, nullptr);
    if (pathLen <= 0) {
        CloseHandle(hRead); CloseHandle(hWrite); CloseHandle(hErrRead); CloseHandle(hErrWrite);
        CloseHandle(hStdInput);
        DebugLogW(L"[ADB] WideCharToMultiByte adbPath failed");
        return false;
    }
    
    // Конвертируем args из wchar_t в char
    char argsA[2048];
    int argsLen = WideCharToMultiByte(CP_ACP, 0, args, -1, argsA, sizeof(argsA), nullptr, nullptr);
    if (argsLen <= 0) {
        CloseHandle(hRead); CloseHandle(hWrite); CloseHandle(hErrRead); CloseHandle(hErrWrite);
        CloseHandle(hStdInput);
        DebugLogW(L"[ADB] WideCharToMultiByte args failed");
        return false;
    }
    
    char cmdA[4096];
    // Добавляем кавычки вокруг пути если есть пробелы
    if (strchr(adbPathA, ' ') != nullptr) {
        sprintf_s(cmdA, sizeof(cmdA), "\"%s\" %s", adbPathA, argsA);
    } else {
        sprintf_s(cmdA, sizeof(cmdA), "%s %s", adbPathA, argsA);
    }
    
    DebugLogFmtW(L"[ADB] CMD: %S", cmdA);
    
    if (!CreateProcessA(NULL, cmdA, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        DWORD err = GetLastError();
        char errBuf[128]; sprintf_s(errBuf, 128, "[ADB] CreateProcessA failed: err=%d\r\n", (int)err);
        OutputDebugStringA(errBuf);
        DebugLogFmtW(L"[ADB] CreateProcessA failed: err=%d", (int)err);
        CloseHandle(si.hStdInput);
        CloseHandle(hRead); CloseHandle(hWrite); CloseHandle(hErrRead); CloseHandle(hErrWrite); 
        return false;
    }
    DebugLogW(L"[ADB] CreateProcessA success");
    
    // Закрываем hWrite ДО ожидания — иначе ADB может буферизировать вывод в pipe
    CloseHandle(hWrite);
    DebugLogW(L"[ADB] hWrite closed, waiting for process...");
    
    // Ждём завершения без таймаута — ADB сам завершится когда будет готов
    DWORD waitResult = WaitForSingleObject(pi.hProcess, INFINITE);
    DebugLogW(L"[ADB] WaitForSingleObject completed");
    
    if (waitResult == WAIT_TIMEOUT) {
        DebugLogW(L"[ADB] UNEXPECTED: process did not complete with INFINITE timeout");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        CloseHandle(hRead); CloseHandle(hErrRead);
        return false;
    }
    
    // Получаем код завершения процесса
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    DebugLogFmtW(L"[ADB] Process exit code: %d", (int)exitCode);
    
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    
    // Читаем stdout — используем PeekNamedPipe для проверки данных
    DebugLogW(L"[ADB] Reading stdout...");
    DWORD bytesRead = 0; char rawBuf[65536] = ""; char* ptr = rawBuf;
    DWORD totalRead = 0;
    while (totalRead < (DWORD)(sizeof(rawBuf) - 1)) {
        DWORD peekBytes = 0;
        if (!PeekNamedPipe(hRead, NULL, 0, NULL, &peekBytes, NULL)) break;
        if (peekBytes == 0) break;
        
        DWORD toRead = (peekBytes < 4096) ? peekBytes : 4096;
        if (!ReadFile(hRead, ptr, toRead, &bytesRead, NULL) || bytesRead == 0) break;
        ptr += bytesRead;
        totalRead += bytesRead;
    }
    DebugLogFmtW(L"[ADB] stdout bytesRead: %d", (int)totalRead);
    
    // Закрываем hRead перед чтением stderr
    CloseHandle(hRead); hRead = NULL;
    
    // Читаем stderr (ошибки/вывод ADB)
    DebugLogW(L"[ADB] Reading stderr...");
    DWORD errBytesRead = 0; char errBuf[4096] = ""; char* errPtr = errBuf;
    DWORD totalErrRead = 0;
    while (totalErrRead < (DWORD)(sizeof(errBuf) - 1)) {
        DWORD peekErrBytes = 0;
        if (!PeekNamedPipe(hErrRead, NULL, 0, NULL, &peekErrBytes, NULL)) break;
        if (peekErrBytes == 0) break;
        
        DWORD toReadErr = (peekErrBytes < 4096) ? peekErrBytes : 4096;
        if (!ReadFile(hErrRead, errPtr, toReadErr, &errBytesRead, NULL) || errBytesRead == 0) break;
        errPtr += errBytesRead;
        totalErrRead += errBytesRead;
    }
    DebugLogFmtW(L"[ADB] stderr bytesRead: %d", (int)totalErrRead);
    
    CloseHandle(hErrRead);
    
    // Логирование stderr если есть
    if (totalErrRead > 0) {
        char errOut[4096]; DWORD errLen = (DWORD)strlen(errBuf);
        if (errLen > 4095) errLen = 4095;
        memcpy(errOut, errBuf, errLen);
        errOut[errLen] = '\0';
        DebugLogFmtW(L"[ADB] STDERR: %s", errOut);
    }
    
    // Конвертируем UTF-8 в UTF-16 для вывода (ADB возвращает UTF-8)
    if (output && outputSize > 0 && totalRead > 0) {
        // Сначала логируем сырые байты для отладки
        char debugBuf[1024];
        DWORD debugLen = (totalRead > 1023) ? 1023 : (DWORD)totalRead;
        memcpy(debugBuf, rawBuf, debugLen);
        debugBuf[debugLen] = '\0';
        DebugLogFmtW(L"[ADB] stdout RAW (%d байт): %s", (int)totalRead, debugBuf);
        
        int wLen = MultiByteToWideChar(CP_UTF8, 0, rawBuf, (int)totalRead, nullptr, 0);
        DebugLogFmtW(L"[ADB] UTF-8 convert wLen=%d", wLen);
        
        if (wLen > 0) {
            wchar_t* wBuf = new wchar_t[wLen + 1];
            int result = MultiByteToWideChar(CP_UTF8, 0, rawBuf, (int)totalRead, wBuf, wLen);
            DebugLogFmtW(L"[ADB] MultiByteToWideChar result=%d", result);
            wBuf[wLen] = L'\0';
            
            // Убираем \r\n
            DWORD maxC = outputSize / sizeof(wchar_t) - 1;
            DWORD outLen = (DWORD)wcslen(wBuf);
            if (outLen > maxC) outLen = maxC;
            wcsncpy_s(output, outputSize, wBuf, outLen);
            output[outLen] = L'\0';
            DebugLogFmtW(L"[ADB] final output (%d символов): %s", (int)outLen, output);
            delete[] wBuf;
        } else {
            output[0] = L'\0';
        }
    } else if (output && outputSize > 0) {
        output[0] = L'\0';
    }
    return true;
}

static bool AdbShellList(const wchar_t* serial, const wchar_t* remotePath, wchar_t* fileList, DWORD fileListSize)
{
    wchar_t cmd[1024];
    
    // Определяем путь для команды ls
    // Используем "ls -l" для получения детальной информации о файлах
    // Формат вывода ls -l: drwxrwx--- 2 root everybody 3488 2022-08-12 10:12 Browser
    // Первый символ: 'd' = директория, '-' = файл
    if (remotePath == nullptr || remotePath[0] == L'\0') {
        // Корень устройства — ls -l без аргумента
        if (serial && serial[0] != L'\0') {
            swprintf_s(cmd, 1024, L"-s %s shell ls -l", serial);
        } else {
            swprintf_s(cmd, 1024, L"shell ls -l");
        }
    } else {
        // remotePath должен быть абсолютным путём (начинаться с /)
        if (remotePath[0] == L'/') {
            if (serial && serial[0] != L'\0') {
                swprintf_s(cmd, 1024, L"-s %s shell ls -l \"%s\"", serial, remotePath);
            } else {
                swprintf_s(cmd, 1024, L"shell ls -l \"%s\"", remotePath);
            }
        } else {
            // Если путь не абсолютный — возвращаем ошибку
            DebugLogFmtW(L"[SHELL] ERROR: путь должен быть абсолютным: %s", remotePath);
            return false;
        }
    }
    
    DebugLogFmtW(L"[SHELL] CMD: %s", cmd);
    
    wchar_t output[65536] = L"";
    if (!AdbExec(cmd, output, sizeof(output))) {
        DebugLogW(L"[SHELL] AdbExec вернул false");
        return false;
    }
    
    // Логирование вывода для отладки
    if (wcslen(output) > 0) {
        DebugLogFmtW(L"[SHELL] ls output (%d символов):\n%s", (int)wcslen(output), output);
    } else {
        DebugLogW(L"[SHELL] ls output: пусто");
    }
    
    fileList[0] = L'\0'; DWORD fc = 0;
    wchar_t* copy = new wchar_t[wcslen(output) + 1]; wcscpy_s(copy, wcslen(output) + 1, output);
    wchar_t* lineStart = copy; wchar_t* lineEnd;
    
    // Сначала заменяем все \n на \r\n для единообразного разбора
    wchar_t* normalized = new wchar_t[wcslen(output) + 1];
    wchar_t* dst = normalized;
    const wchar_t* src = output;
    while (*src != L'\0') {
        if (*src == L'\n' && (src == output || *(src - 1)) != L'\r') {
            *dst++ = L'\r';
            *dst++ = L'\n';
        } else if (*src != L'\r') {  // пропускаем старые \r
            *dst++ = *src;
        }
        src++;
    }
    *dst = L'\0';
    
    lineStart = normalized;
    while ((lineEnd = wcsstr(lineStart, L"\r\n")) != nullptr) {
        *lineEnd = L'\0';
        
        // Пропускаем пустые строки и "List of devices"
        if (lineStart[0] != L'\0' && wcsstr(lineStart, L"List of devices") == nullptr) {
            // Пропускаем строку "total N" — это не файл
            if (_wcsnicmp(lineStart, L"total", 5) == 0) {
                lineStart = lineEnd + 2;
                continue;
            }
            
            // Формат: drwxrwx--- 2 root everybody 3488 2022-08-12 10:12 FileName
            // Первый символ определяет тип: 'd' = директория, '-' = файл
            bool isFolder = (lineStart[0] == L'd');
            
            // Ищем имя файла — оно после последнего пробела/табуляции
            // Формат: drwxrwx--- 2 root everybody 3488 2026-07-01 13:32 MiuiFastConnect
            wchar_t* fileNameStart = lineStart;
            
            // Пропускаем permissions (drwxrwx---) — до первого пробела
            while (*fileNameStart != L'\0' && *fileNameStart != L' ') fileNameStart++;
            // Пропускаем пробелы
            while (*fileNameStart != L'\0' && (*fileNameStart == L' ' || *fileNameStart == L'\t')) fileNameStart++;
            // Пропускаем links (2)
            while (*fileNameStart != L'\0' && *fileNameStart != L' ') fileNameStart++;
            // Пропускаем пробелы
            while (*fileNameStart != L'\0' && (*fileNameStart == L' ' || *fileNameStart == L'\t')) fileNameStart++;
            // Пропускаем owner (root)
            while (*fileNameStart != L'\0' && *fileNameStart != L' ') fileNameStart++;
            // Пропускаем пробелы
            while (*fileNameStart != L'\0' && (*fileNameStart == L' ' || *fileNameStart == L'\t')) fileNameStart++;
            // Пропускаем group (everybody)
            while (*fileNameStart != L'\0' && *fileNameStart != L' ') fileNameStart++;
            // Пропускаем пробелы
            while (*fileNameStart != L'\0' && (*fileNameStart == L' ' || *fileNameStart == L'\t')) fileNameStart++;
            // Пропускаем size (число, например 3488)
            while (*fileNameStart != L'\0' && *fileNameStart != L' ') fileNameStart++;
            // Пропускаем пробелы
            while (*fileNameStart != L'\0' && (*fileNameStart == L' ' || *fileNameStart == L'\t')) fileNameStart++;
            // Пропускаем date (2026-07-01)
            while (*fileNameStart != L'\0' && *fileNameStart != L' ') fileNameStart++;
            // Пропускаем пробелы
            while (*fileNameStart != L'\0' && (*fileNameStart == L' ' || *fileNameStart == L'\t')) fileNameStart++;
            // Пропускаем time (13:32)
            while (*fileNameStart != L'\0' && *fileNameStart != L' ') fileNameStart++;
            // Пропускаем пробелы
            while (*fileNameStart != L'\0' && (*fileNameStart == L' ' || *fileNameStart == L'\t')) fileNameStart++;
            // Теперь fileNameStart указывает на имя файла!
            
            // Логирование для отладки
            wchar_t debugPath[512];
            swprintf_s(debugPath, L"[SHELL] ls line: '%s' -> fileNameStart='%s'", lineStart, fileNameStart);
            DebugLogW(debugPath);
            
            if (fc > 0) wcscat_s(fileList, fileListSize, L",");
            wcscat_s(fileList, fileListSize, L"{\"Name\":\"");
            
            // Копируем имя файла напрямую — используем отдельный указатель!
            wchar_t* jsonDst = fileList + wcslen(fileList);
            for (size_t ei = 0; fileNameStart[ei] != L'\0' && fileNameStart[ei] != L' ' && fileNameStart[ei] != L'\r' && fileNameStart[ei] != L'\n' && ei < 1023; ei++) {
                if (fileNameStart[ei] == L'"') { *jsonDst = L'\\'; jsonDst++; *jsonDst = L'"'; jsonDst++; }
                else { *jsonDst = fileNameStart[ei]; jsonDst++; }
            }
            *jsonDst = L'\0';
            
            wcscat_s(fileList, fileListSize, L"\",\"IsFolder\":");
            wcscat_s(fileList, fileListSize, isFolder ? L"true" : L"false");
            wcscat_s(fileList, fileListSize, L"}");
            fc++;
        }
        lineStart = lineEnd + 2;
    }
    delete[] normalized;
    delete[] copy;
    return fc > 0;
}

// ===== Извлечение модели из вывода adb devices -l =====
static void ExtractModelFromAdbOutput(const wchar_t* adbOutput, const wchar_t* serial, wchar_t* model, DWORD modelSize)
{
    model[0] = L'\0';
    if (!adbOutput || !serial || adbOutput[0] == L'\0' || serial[0] == L'\0') return;
    
    // Ищем строку с этим серийным номером
    const wchar_t* lineStart = wcsstr(adbOutput, serial);
    if (!lineStart) return;
    
    // Находим конец строки
    const wchar_t* lineEnd = wcsstr(lineStart, L"\r\n");
    if (!lineEnd) lineEnd = wcsstr(lineStart, L"\n");
    if (!lineEnd) lineEnd = lineStart + wcslen(lineStart);
    
    // Ищем "model:" в строке
    const wchar_t* modelPos = wcsstr(lineStart, L"model:");
    if (!modelPos || modelPos >= lineEnd) return;
    
    modelPos += 6; // пропускаем "model:"
    
    // Копируем модель до пробела или конца строки
    DWORD modelLen = 0;
    while (*modelPos != L'\0' && *modelPos != L' ' && *modelPos != L'\t' && *modelPos != L'\r' && *modelPos != L'\n' 
           && modelPos < lineEnd && modelLen < modelSize - 1) {
        model[modelLen++] = *modelPos++;
    }
    model[modelLen] = L'\0';
}

// ===== Debug logging =====
static void DebugLogW(const wchar_t* msg) { OutputDebugStringW(msg); OutputDebugStringW(L"\r\n"); }
static void DebugLogFmtW(const wchar_t* fmt, ...) {
    va_list args; va_start(args, fmt); wchar_t buf[1024]; vswprintf_s(buf, 1024, fmt, args); va_end(args);
    OutputDebugStringW(buf); OutputDebugStringW(L"\r\n");
}

// Логирование с указанием метода и шага
static const wchar_t* g_currentMethod = L"None";

static void SetMethodContext(const wchar_t* method) { g_currentMethod = method; }
static void DebugLogStepW(const wchar_t* step) {
    wchar_t msg[1024];
    swprintf_s(msg, L"[DBG] %s: %s", g_currentMethod, step);
    OutputDebugStringW(msg);
    OutputDebugStringW(L"\r\n");
    WriteLog(msg);
}

uint32_t ADBFileDriver::EnumerateMtpDevices()
{
    m_DeviceCount = 0; m_DeviceList[0] = L'\0'; m_CurrentPath[0] = L'\0';
    wchar_t buf[256];
    
    SetMethodContext(L"EnumerateDevices");
    DebugLogStepW(L"ENTER — начало ПеречислитьУстройства");
    
    // Находим adb.exe
    if (!FindAdbExe()) {
        DebugLogStepW(L"ERROR: adb.exe не найден");
        wcscpy_s(m_Status, 512, L"Ошибка: adb.exe не найден");
        wcscpy_s(m_DeviceList, 8192, L"[]");
        return 0;
    }
    DebugLogStepW(L"adb.exe найден, путь:");
    DebugLogFmtW(L"[ENUM] adb.exe path: %s", g_adbPath);
    
    // === ВЫПОЛНЯЕМ ПЕРВУЮ КОМАНДУ devices -l ===
    // ADB автоматически запускает сервер при первой команде если он не запущен
    DebugLogStepW(L"Вызов adb devices -l (автоматический запуск сервера)");
    wchar_t adbDevices[65536] = L"";
    bool adbResult = AdbExec(L"devices -l", adbDevices, sizeof(adbDevices));
    
    // Логирование вывода adb devices
    if (adbResult) {
        if (wcslen(adbDevices) > 0 && wcslen(adbDevices) < 4096) {
            DebugLogFmtW(L"[ENUM] devices -l УСПЕХ (%d символов):\n%s", (int)wcslen(adbDevices), adbDevices);
        } else if (wcslen(adbDevices) >= 4096) {
            DebugLogW(L"[ENUM] devices -l УСПЕХ (вывод слишком длинный для логирования)");
        } else {
            DebugLogW(L"[ENUM] devices -l УСПЕХ (пустой ответ)");
        }
    } else {
        DebugLogW(L"[ENUM] devices -l ОШИБКА");
        wcscpy_s(m_Status, 512, L"При запуске ADB возникла ошибка");
        wcscpy_s(m_DeviceList, 8192, L"[]");
        return 0;
    }
    
    // Логирование полного вывода adb devices
    if (wcslen(adbDevices) > 0 && wcslen(adbDevices) < 4096) {
        wchar_t logLine[4096];
        swprintf_s(logLine, L"[ENUM] adb devices FULL OUTPUT:\n%s", adbDevices);
        DebugLogW(logLine);
        
        // Логирование построчно
        wchar_t* parseCopy = new wchar_t[wcslen(adbDevices) + 1];
        wcscpy_s(parseCopy, wcslen(adbDevices) + 1, adbDevices);
        wchar_t* lineStart = parseCopy;
        wchar_t* lineEnd;
        int lineNum = 0;
        while ((lineEnd = wcsstr(lineStart, L"\r\n")) != nullptr) {
            *lineEnd = L'\0';
            if (lineStart[0] != L'\0') {
                DebugLogFmtW(L"[ENUM] adb line[%d]: '%s'", lineNum, lineStart);
                lineNum++;
            }
            lineStart = lineEnd + 2;
        }
        if (lineStart[0] != L'\0') {
            DebugLogFmtW(L"[ENUM] adb line[%d]: '%s'", lineNum, lineStart);
        }
        delete[] parseCopy;
    } else {
        DebugLogW(L"[ENUM] adb devices output is empty or too long");
    }
    
    HDEVINFO hDevInfo = SetupDiGetClassDevsW((GUID*)NULL, L"USB", NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        swprintf_s(buf, L"[ADBFileDriver] SetupDiGetClassDevsW failed: error=%d", err);
        DebugLogW(buf); wcscpy_s(m_DeviceList, 8192, L"[]"); return 0;
    }
    
    int totalDev = 0, usbDev = 0, androidVid = 0, connected = 0, mtpFound = 0;
    SP_DEVINFO_DATA devInfo = { sizeof(devInfo) };
    
    for (DWORD i = 0; ; i++) {
        if (!SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo)) break;
        totalDev++;
        
        wchar_t pnpId[1024] = L"";
        CM_Get_Device_IDW(devInfo.DevInst, pnpId, 1024, 0);
        if (pnpId[0] == L'\0') continue;
        
        wchar_t friendlyName[512] = L"";
        SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_FRIENDLYNAME, NULL, (PBYTE)friendlyName, 512, NULL);
        if (friendlyName[0] == L'\0') SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_DEVICEDESC, NULL, (PBYTE)friendlyName, 512, NULL);
        
        if (!IsAndroidVendorId(pnpId)) continue;
        usbDev++; androidVid++;
        
        DWORD status, errMsg;
        if (CM_Get_DevNode_Status(&status, &errMsg, devInfo.DevInst, 0) != CR_SUCCESS) continue;
        
        HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        bool connected2 = (hKey != INVALID_HANDLE_VALUE);
        if (hKey != INVALID_HANDLE_VALUE) CloseHandle(hKey);
        if (!connected2) continue;
        connected++;
        
        wchar_t pnpId2[1024] = L"";
        CM_Get_Device_IDW(devInfo.DevInst, pnpId2, 1024, 0);
        
        // Не фильтруем по MTP — все Android USB устройства подходят
        mtpFound++;
        
        // Извлекаем Serial из PnpId (последняя часть после последнего \)
        wchar_t serialNumber[256] = L"";
        const wchar_t* lastSlash = wcsrchr(pnpId, L'\\');
        if (lastSlash && *(lastSlash + 1) != L'\0') {
            wcscpy_s(serialNumber, 256, lastSlash + 1);
        }
        
        // Проверяем есть ли это устройство в списке ADB
        // Формат строки: "SERIAL\tstatus ..." или "SERIAL  status ..." (пробелы вместо табуляции)
        bool isAdbDevice = false;
        if (serialNumber[0] != L'\0') {
            // Ищем "SERIAL" followed by whitespace and "device"
            const wchar_t* searchPos = wcsstr(adbDevices, serialNumber);
            if (searchPos != nullptr) {
                // Проверяем что после serial идёт \t или пробелы и затем "device"
                const wchar_t* afterSerial = searchPos + wcslen(serialNumber);
                // Пропускаем табуляции и пробелы
                while (*afterSerial == L'\t' || *afterSerial == L' ') afterSerial++;
                if (wcsncmp(afterSerial, L"device", 6) == 0 || wcsncmp(afterSerial, L"offline", 7) == 0) {
                    isAdbDevice = true;
                }
            }
        }
        
        DebugLogFmtW(L"[ENUM] Device: PnpId=%s, Serial=%s, ADB=%d", pnpId, serialNumber, isAdbDevice ? 1 : 0);
        
        // Если устройство не найдено в списке ADB, пропускаем
        if (!isAdbDevice) {
            DebugLogW(L"[ENUM]   Skipping — not in adb devices list");
            // Добавляем подробную информацию для отладки
            DebugLogFmtW(L"[ENUM]   Checked pattern: '%s\\tdevice'", serialNumber);
            
            // Покажем все ADB serial для сравнения
            wchar_t* adbCopy = new wchar_t[wcslen(adbDevices) + 1];
            wcscpy_s(adbCopy, wcslen(adbDevices) + 1, adbDevices);
            wchar_t* adbLineStart = adbCopy;
            wchar_t* adbLineEnd;
            int adbLineNum = 0;
            DebugLogW(L"[ENUM]   ADB devices list content:");
            while ((adbLineEnd = wcsstr(adbLineStart, L"\r\n")) != nullptr) {
                *adbLineEnd = L'\0';
                if (adbLineStart[0] != L'\0' && wcsstr(adbLineStart, L"List of devices") == nullptr) {
                    // Извлекаем serial из строки adb (первое поле до '\t')
                    wchar_t* tabPos = wcschr(adbLineStart, L'\t');
                    if (tabPos) {
                        *tabPos = L'\0';
                        DebugLogFmtW(L"[ENUM]   ADB device[%d]: serial='%s', full='%s'", adbLineNum, adbLineStart, adbLineStart);
                    } else {
                        DebugLogFmtW(L"[ENUM]   ADB device[%d]: '%s'", adbLineNum, adbLineStart);
                    }
                }
                adbLineStart = adbLineEnd + 2;
                adbLineNum++;
            }
            delete[] adbCopy;
            continue;
        }
        
        if (m_DeviceCount == 0) wcscpy_s(m_DeviceList, 8192, L"[");
        else wcscat_s(m_DeviceList, 8192, L",");
        
        // Формируем JSON вручную чтобы избежать проблем с экранированием
        wchar_t jsonDevice[4096] = L"";
        wcscat_s(jsonDevice, 4096, L"{\"Serial\":\"");
        
        // Экранируем serialNumber
        if (serialNumber[0] != L'\0') {
            for (size_t si = 0; serialNumber[si] != L'\0' && si < 256; si++) {
                if (serialNumber[si] == L'"') wcscat_s(jsonDevice, 4096, L"\\\"");
                else if (serialNumber[si] == L'\\') wcscat_s(jsonDevice, 4096, L"\\\\");
                else {
                    wchar_t ch[2] = { serialNumber[si], L'\0' };
                    wcscat_s(jsonDevice, 4096, ch);
                }
            }
        }
        wcscat_s(jsonDevice, 4096, L"\",\"Name\":\"");
        
        // Экранируем Name — используем модель из adb devices -l
        wchar_t deviceModel[256] = L"";
        ExtractModelFromAdbOutput(adbDevices, serialNumber, deviceModel, sizeof(deviceModel) / sizeof(wchar_t));
        
        if (deviceModel[0] != L'\0') {
            // Используем модель устройства
            for (size_t si = 0; deviceModel[si] != L'\0' && si < 256; si++) {
                if (deviceModel[si] == L'"') wcscat_s(jsonDevice, 4096, L"\\\"");
                else if (deviceModel[si] == L'\\') wcscat_s(jsonDevice, 4096, L"\\\\");
                else {
                    wchar_t ch[2] = { deviceModel[si], L'\0' };
                    wcscat_s(jsonDevice, 4096, ch);
                }
            }
        } else if (friendlyName[0] != L'\0') {
            // fallback: friendlyName USB устройства
            for (size_t si = 0; friendlyName[si] != L'\0' && si < 512; si++) {
                if (friendlyName[si] == L'"') wcscat_s(jsonDevice, 4096, L"\\\"");
                else if (friendlyName[si] == L'\\') wcscat_s(jsonDevice, 4096, L"\\\\");
                else {
                    wchar_t ch[2] = { friendlyName[si], L'\0' };
                    wcscat_s(jsonDevice, 4096, ch);
                }
            }
        } else {
            wcscat_s(jsonDevice, 4096, L"Unknown");
        }
        
        DebugLogFmtW(L"[ENUM] Device model: %s", deviceModel[0] != L'\0' ? deviceModel : friendlyName);
        wcscat_s(jsonDevice, 4096, L"\"}");
        
        wcscat_s(m_DeviceList, 8192, jsonDevice);
        m_DeviceCount++;
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    if (m_DeviceCount > 0) wcscat_s(m_DeviceList, 8192, L"]");
    else wcscpy_s(m_DeviceList, 8192, L"[]");
    
    DebugLogFmtW(L"[ENUM] Result: deviceCount=%d", m_DeviceCount);
    return m_DeviceCount;
}

bool ADBFileDriver::ConnectToDevice(const wchar_t* deviceName)
{
    wchar_t dbgMsg[512], adbOutput[65536];
    SetMethodContext(L"Connect");
    DebugLogStepW(L"ENTER — начало Подключить");
    
    if (!FindAdbExe()) {
        DebugLogStepW(L"ERROR: adb.exe не найден");
        wcscpy_s(m_Status, 512, L"Ошибка: adb.exe не найден");
        return false;
    }
    DebugLogStepW(L"adb.exe найден");
    
    // Проверяем что подключено
    if (m_bConnected) { 
        DebugLogStepW(L"Устройство уже подключено, отключаем");
        DisconnectDevice(); 
    }
    
    // Получаем список устройств
    DebugLogStepW(L"Вызов adb devices -l");
    wchar_t adbDevices[65536] = L"";
    bool adbResult = AdbExec(L"devices -l", adbDevices, sizeof(adbDevices));
    
    if (!adbResult) {
        DebugLogW(L"[CONNECT] devices -l ОШИБКА");
        wcscpy_s(m_Status, 512, L"При запуске ADB возникла ошибка");
        return false;
    }
    
    if (wcslen(adbDevices) > 0 && wcslen(adbDevices) < 4096) {
        DebugLogFmtW(L"[CONNECT] devices -l УСПЕХ (%d символов):\n%s", (int)wcslen(adbDevices), adbDevices);
    }
    
    // Парсим список устройств
    const wchar_t* targetSerial = nullptr;
    
    // deviceName — это Serial устройства (или пустая строка для первого)
    if (deviceName != nullptr && deviceName[0] != L'\0') {
        // Ищем устройство по Serial в m_DeviceList
        DebugLogFmtW(L"[CONNECT] Ищем по Serial: %s", deviceName);
        
        wchar_t pattern[512];
        swprintf_s(pattern, 512, L"\"Serial\":\"%s\"", deviceName);
        
        const wchar_t* matchPos = wcsstr(m_DeviceList, pattern);
        if (matchPos) {
            // Нашли совпадение — Serial уже известен
            wcscpy_s(m_AdbSerial, 256, deviceName);
            targetSerial = m_AdbSerial;
            DebugLogFmtW(L"[CONNECT] Найдено по Serial: %s", targetSerial);
        } else {
            DebugLogFmtW(L"[CONNECT] Устройство с Serial '%s' НЕ найдено в m_DeviceList", deviceName);
            DebugLogFmtW(L"[CONNECT] m_DeviceList = %s", m_DeviceList);
        }
    }
    
    // Иначе берём первое устройство из последнего списка
    if (targetSerial == nullptr) {
        DebugLogW(L"[CONNECT] targetSerial == nullptr — выбираем первое устройство из m_DeviceList");
        
        // Ищем первый Serial в JSON: {"Serial":"...","Name":"..."}
        const wchar_t* searchPos = m_DeviceList;
        const wchar_t* serialKey = wcsstr(searchPos, L"\"Serial\":\"");
        if (serialKey) {
            const wchar_t* sf = serialKey + 10;
            const wchar_t* se = wcsstr(sf, L"\"");
            if (se) {
                size_t sl = se - sf;
                if (sl > 0 && sl < 256) { wcsncpy_s(m_AdbSerial, 256, sf, sl); m_AdbSerial[sl] = L'\0'; }
                targetSerial = m_AdbSerial;
                DebugLogFmtW(L"[CONNECT] Первое устройство: %s", targetSerial);
                
                // Сохраняем Serial первого устройства
                wcscpy_s(m_LastDeviceSerial, 256, m_AdbSerial);
            }
        }
        
        if (targetSerial == nullptr) {
            DebugLogW(L"[CONNECT] Не нашли устройств в m_DeviceList");
        }
    }
    
    if (targetSerial == nullptr || targetSerial[0] == L'\0') {
        DebugLogW(L"[CONNECT] ERROR: ADB-устройство не найдено");
        wcscpy_s(m_Status, 512, L"Ошибка: ADB-устройство не найдено");
        return false;
    }
    
    DebugLogFmtW(L"[CONNECT] Подключено к: %s", targetSerial);
    m_bConnected = true;
    wcscpy_s(m_Status, 512, L"Подключено (ADB)");
    wcscpy_s(m_CurrentPath, 1024, L"");
    
    DebugLogStepW(L"Подключено успешно");
    return true;
}

void ADBFileDriver::DisconnectDevice()
{
    m_bConnected = false;
    m_DeviceId[0] = L'\0';
    m_AdbSerial[0] = L'\0';
    m_CurrentPath[0] = L'\0';
    wcscpy_s(m_Status, 512, L"Не подключен");
    
    // Завершаем ADB сервер
    if (FindAdbExe()) {
        wchar_t adbOutput[1024] = L"";
        AdbExec(L"kill-server", adbOutput, sizeof(adbOutput));
        DebugLogW(L"[DISCONNECT] ADB сервер завершён");
    }
}

uint32_t ADBFileDriver::EnumerateFilesOnDevice(const wchar_t* remotePath)
{
    m_FileCount = 0; m_FileList[0] = L'\0';
    if (!m_bConnected) { wcscpy_s(m_FileList, 65536, L"[]"); return 0; }
    
    // Обновить текущий каталог
    if (remotePath && remotePath[0] != L'\0') {
        wcscpy_s(m_CurrentPath, 1024, remotePath);
    } else {
        m_CurrentPath[0] = L'\0';
    }
    
    if (!AdbShellList(m_AdbSerial, remotePath ? remotePath : L"/", m_FileList, sizeof(m_FileList))) {
        wcscpy_s(m_FileList, 65536, L"[]"); return 0;
    }
    m_FileCount = 0;
    for (const wchar_t* p = m_FileList; *p; p++) if (*p == L'{') m_FileCount++;
    return m_FileCount;
}

uint32_t ADBFileDriver::ListFileNames(const wchar_t* remotePath)
{
    m_FileCount = 0; m_FileList[0] = L'\0';
    if (!m_bConnected) { wcscpy_s(m_FileList, 65536, L"[]"); return 0; }
    
    // Обновить текущий каталог
    if (remotePath && remotePath[0] != L'\0') {
        wcscpy_s(m_CurrentPath, 1024, remotePath);
    } else {
        m_CurrentPath[0] = L'\0';
    }
    
    if (!AdbShellList(m_AdbSerial, remotePath ? remotePath : L"/", m_FileList, sizeof(m_FileList))) {
        wcscpy_s(m_FileList, 65536, L"[]"); return 0;
    }
    m_FileCount = 0;
    for (const wchar_t* p = m_FileList; *p; p++) if (*p == L'{') m_FileCount++;
    return m_FileCount;
}

bool ADBFileDriver::DownloadFile(const wchar_t* fileName, wchar_t** content, uint32_t* contentSize)
{
    if (!m_bConnected) return false;
    
    wchar_t remotePath[1024];
    if (m_CurrentPath[0] != L'\0') swprintf_s(remotePath, 1024, L"%s/%s", m_CurrentPath, fileName);
    else swprintf_s(remotePath, 1024, L"/%s", fileName);
    
    // Создаём временный файл для временного хранения
    wchar_t tempPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempPath)) return false;
    wchar_t tempFile[MAX_PATH];
    GetTempFileNameW(tempPath, L"ADB", 0, tempFile);
    
    // Скачиваем файл во временный файл
    wchar_t cmd[2048];
    if (m_AdbSerial[0] != L'\0') {
        swprintf_s(cmd, 2048, L"-s %s pull \"%s\" \"%s\"", m_AdbSerial, remotePath, tempFile);
    } else {
        swprintf_s(cmd, 2048, L"pull \"%s\" \"%s\"", remotePath, tempFile);
    }
    
    if (!AdbExec(cmd, nullptr, 0)) {
        DeleteFileW(tempFile);
        return false;
    }
    
    // Читаем содержимое временного файла
    HANDLE hFile = CreateFileW(tempFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DeleteFileW(tempFile);
        return false;
    }
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile);
        DeleteFileW(tempFile);
        if (content) *content = nullptr;
        if (contentSize) *contentSize = 0;
        return true; // Пустой файл — не ошибка
    }
    
    // Читаем файл как UTF-8
    char* buffer = new (std::nothrow) char[fileSize + 1];
    if (!buffer) {
        CloseHandle(hFile);
        DeleteFileW(tempFile);
        return false;
    }
    
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer, fileSize, &bytesRead, NULL)) {
        delete[] buffer;
        CloseHandle(hFile);
        DeleteFileW(tempFile);
        return false;
    }
    CloseHandle(hFile);
    
    // Конвертируем UTF-8 в UTF-16
    int wLen = MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, NULL, 0);
    if (wLen <= 0) {
        delete[] buffer;
        DeleteFileW(tempFile);
        return false;
    }
    
    wchar_t* wBuffer = new (std::nothrow) wchar_t[wLen + 1];
    if (!wBuffer) {
        delete[] buffer;
        DeleteFileW(tempFile);
        return false;
    }
    
    MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, wBuffer, wLen);
    wBuffer[wLen] = L'\0';
    
    // Возвращаем содержимое через параметр
    if (content && contentSize) {
        *contentSize = (uint32_t)(wLen + 1); // с терминальным нулём
        *content = new (std::nothrow) wchar_t[*contentSize];
        if (*content) {
            wcscpy_s(*content, *contentSize, wBuffer);
        }
    }
    
    delete[] buffer;
    delete[] wBuffer;
    DeleteFileW(tempFile);
    return true;
}

bool ADBFileDriver::UploadFile(const wchar_t* remoteName, const wchar_t* content, uint32_t contentLen)
{
    if (!m_bConnected) return false;
    
    // Создаём временный файл
    wchar_t tempPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempPath)) return false;
    wchar_t tempFile[MAX_PATH];
    GetTempFileNameW(tempPath, L"ADB", 0, tempFile);
    
    // Записываем содержимое во временный файл как UTF-8
    HANDLE hFile = CreateFileW(tempFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    // Конвертируем UTF-16 в UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, content, contentLen, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) {
        CloseHandle(hFile);
        DeleteFileW(tempFile);
        return false;
    }
    
    char* utf8Buffer = new (std::nothrow) char[utf8Len + 1];
    if (!utf8Buffer) {
        CloseHandle(hFile);
        DeleteFileW(tempFile);
        return false;
    }
    
    WideCharToMultiByte(CP_UTF8, 0, content, contentLen, utf8Buffer, utf8Len, NULL, NULL);
    utf8Buffer[utf8Len] = '\0';
    
    DWORD bytesWritten = 0;
    WriteFile(hFile, utf8Buffer, utf8Len, &bytesWritten, NULL);
    CloseHandle(hFile);
    delete[] utf8Buffer;
    
    // Загружаем файл на устройство
    wchar_t remotePath[1024];
    if (m_CurrentPath[0] != L'\0') swprintf_s(remotePath, 1024, L"%s/%s", m_CurrentPath, remoteName);
    else swprintf_s(remotePath, 1024, L"/%s", remoteName);
    
    wchar_t cmd[2048];
    if (m_AdbSerial[0] != L'\0') swprintf_s(cmd, 2048, L"-s %s push \"%s\" \"%s\"", m_AdbSerial, tempFile, remotePath);
    else swprintf_s(cmd, 2048, L"push \"%s\" \"%s\"", tempFile, remotePath);
    
    bool result = AdbExec(cmd, nullptr, 0);
    
    // Удаляем временный файл
    DeleteFileW(tempFile);
    
    return result;
}

bool ADBFileDriver::DeleteFile(const wchar_t* fileName)
{
    if (!m_bConnected) return false;
    wchar_t remotePath[1024];
    if (m_CurrentPath[0] != L'\0') swprintf_s(remotePath, 1024, L"%s/%s", m_CurrentPath, fileName);
    else swprintf_s(remotePath, 1024, L"/%s", fileName);
    
    wchar_t cmd[2048];
    if (m_AdbSerial[0] != L'\0') swprintf_s(cmd, 2048, L"-s %s shell rm \"%s\"", m_AdbSerial, remotePath);
    else swprintf_s(cmd, 2048, L"shell rm \"%s\"", remotePath);
    return AdbExec(cmd, nullptr, 0);
}

bool ADBFileDriver::FindFileByName(const wchar_t* fileName, const wchar_t*)
{
    if (!m_bConnected) return false;
    wchar_t searchPath[1024];
    if (m_CurrentPath[0] != L'\0') swprintf_s(searchPath, 1024, L"%s", m_CurrentPath);
    else wcscpy_s(searchPath, 1024, L"/");
    
    wchar_t cmd[2048];
    if (m_AdbSerial[0] != L'\0') swprintf_s(cmd, 2048, L"-s %s shell find \"%s\" -name \"%s\" -maxdepth 1", m_AdbSerial, searchPath, fileName);
    else swprintf_s(cmd, 2048, L"shell find \"%s\" -name \"%s\" -maxdepth 1", searchPath, fileName);
    
    wchar_t output[4096] = L"";
    if (AdbExec(cmd, output, sizeof(output)) && wcsstr(output, fileName) != nullptr) return true;
    return false;
}

bool ADBFileDriver::CallAsFunc(const long lMethodNum, tVariant* pvarRetValue, tVariant* paParams, const long)
{
    // Детальное логирование входа в метод
    wchar_t enterMsg[512];
    swprintf_s(enterMsg, L"[CALL] CallAsFunc ENTER: lMethodNum=%ld, paParams=%p, pvarRetValue=%p, VT=0x%04X", 
               lMethodNum, (void*)paParams, (void*)pvarRetValue, paParams ? (paParams[0].vt) : 0xFFFFFFFF);
    DebugLogW(enterMsg);
    DebugLogW(L"[CALL] CallAsFunc: BEFORE tVarInit");
    
    // Инициализируем возвращаемое значение перед использованием
    tVarInit(pvarRetValue);
    wchar_t vtMsg[256];
    swprintf_s(vtMsg, L"[CALL] CallAsFunc: AFTER tVarInit, VT=0x%04X", pvarRetValue->vt);
    DebugLogW(vtMsg);
    
    __try {
        switch (lMethodNum) {
            case 0: {
                OutputDebugStringW(L"[CALL] case 0: EnumerateDevices START");
                uint32_t count = EnumerateMtpDevices();
                size_t len = wcslen(m_DeviceList) + 1;
                pvarRetValue->pwstrVal = nullptr;
                pvarRetValue->wstrLen = 0;
                if (len > 1 && m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarRetValue->pwstrVal, m_DeviceList, (uint32_t)len);
                    pvarRetValue->wstrLen = (uint32_t)wcslen(m_DeviceList);
                }
                else {
                    // Если память не удалось выделить, возвращаем пустую строку
                    static const wchar_t emptyStr[] = L"";
                    pvarRetValue->pwstrVal = nullptr;
                    pvarRetValue->wstrLen = 0;
                }
                TV_VT(pvarRetValue) = VTYPE_PWSTR;
                return true;
            }
            case 1: {
                OutputDebugStringW(L"[CALL] case 1: Connect START");
                
                // Логирование параметров
                if (paParams) {
                    wchar_t paramMsg[512];
                    swprintf_s(paramMsg, L"[CALL] case 1: paParams=%p, paParams[0].vt=0x%04X, paParams[0].pwstrVal=%p",
                               (void*)paParams, paParams[0].vt, (void*)paParams[0].pwstrVal);
                    DebugLogW(paramMsg);
                    
                    if (paParams[0].pwstrVal != nullptr) {
                        wchar_t paramName[512];
                        swprintf_s(paramName, L"[CALL] case 1: deviceName='%s'", paParams[0].pwstrVal);
                        DebugLogW(paramName);
                    } else {
                        DebugLogW(L"[CALL] case 1: deviceName is NULL");
                    }
                } else {
                    DebugLogW(L"[CALL] case 1: paParams is NULL");
                }
                
                DebugLogW(L"[CALL] case 1: BEFORE ConnectToDevice");
                bool result = (paParams && paParams[0].pwstrVal != nullptr) ? ConnectToDevice(paParams[0].pwstrVal) : ConnectToDevice(nullptr);
                wchar_t resMsg[128];
                swprintf_s(resMsg, L"[CALL] case 1: AFTER ConnectToDevice, result=%d", result ? 1 : 0);
                DebugLogW(resMsg);
                
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = result ? VARIANT_TRUE : VARIANT_FALSE;
                DebugLogW(L"[CALL] case 1: BEFORE return");
                return true;
            }
            case 2: 
                DisconnectDevice(); 
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = VARIANT_TRUE;
                return true;
            case 3: {
                const wchar_t* path = (paParams && paParams[0].pwstrVal != nullptr) ? paParams[0].pwstrVal : L"";
                m_FileCount = EnumerateFilesOnDevice(path);
                size_t len = wcslen(m_FileList) + 1;
                pvarRetValue->pwstrVal = nullptr;
                pvarRetValue->wstrLen = 0;
                if (len > 1 && m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarRetValue->pwstrVal, m_FileList, (uint32_t)len);
                    pvarRetValue->wstrLen = (uint32_t)wcslen(m_FileList);
                }
                else {
                    pvarRetValue->pwstrVal = nullptr;
                    pvarRetValue->wstrLen = 0;
                }
                TV_VT(pvarRetValue) = VTYPE_PWSTR;
                return true;
            }
            case 4: {
                // СкачатьФайл(ИмяФайла) → возвращает содержимое файла
                const wchar_t* fn = (paParams && paParams[0].pwstrVal != nullptr) ? paParams[0].pwstrVal : L"";
                
                wchar_t* fileContent = nullptr;
                uint32_t contentSize = 0;
                
                bool success = DownloadFile(fn, &fileContent, &contentSize);
                
                pvarRetValue->pwstrVal = nullptr;
                pvarRetValue->wstrLen = 0;
                
                if (success && fileContent != nullptr) {
                    if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)(contentSize * sizeof(WCHAR_T)))) {
                        memcpy(pvarRetValue->pwstrVal, fileContent, contentSize * sizeof(WCHAR_T));
                        pvarRetValue->wstrLen = contentSize - 1; // без терминального нуля
                    }
                    delete[] fileContent;
                }
                else {
                    // Возвращаем пустую строку при ошибке
                    static const wchar_t emptyStr[] = L"";
                    if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, 2)) {
                        pvarRetValue->pwstrVal[0] = L'\0';
                        pvarRetValue->wstrLen = 0;
                    }
                }
                
                TV_VT(pvarRetValue) = VTYPE_PWSTR;
                return true;
            }
            case 5: {
                // ЗагрузитьФайл(ИмяНаУстройстве, Содержание)
                const wchar_t* rn = (paParams && paParams[0].pwstrVal != nullptr) ? paParams[0].pwstrVal : L"";
                
                // Получаем содержание — строка
                const wchar_t* content = nullptr;
                uint32_t contentLen = 0;
                
                if (paParams && paParams[1].vt == VTYPE_PWSTR && paParams[1].pwstrVal != nullptr) {
                    content = paParams[1].pwstrVal;
                    contentLen = (uint32_t)wcslen(content);
                }
                
                bool success = UploadFile(rn, content ? content : L"", contentLen);
                
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = success ? VARIANT_TRUE : VARIANT_FALSE;
                return true;
            }
            case 6: {
                const wchar_t* fn = (paParams && paParams[0].pwstrVal != nullptr) ? paParams[0].pwstrVal : L"";
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = DeleteFile(fn) ? VARIANT_TRUE : VARIANT_FALSE;
                return true;
            }
            case 7: {
                const wchar_t* rp = (paParams && paParams[0].pwstrVal != nullptr) ? paParams[0].pwstrVal : L"";
                m_FileCount = ListFileNames(rp);
                size_t len = wcslen(m_FileList) + 1;
                pvarRetValue->pwstrVal = nullptr;
                pvarRetValue->wstrLen = 0;
                if (len > 1 && m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarRetValue->pwstrVal, m_FileList, (uint32_t)len);
                    pvarRetValue->wstrLen = (uint32_t)wcslen(m_FileList);
                }
                else {
                    pvarRetValue->pwstrVal = nullptr;
                    pvarRetValue->wstrLen = 0;
                }
                TV_VT(pvarRetValue) = VTYPE_PWSTR;
                return true;
            }
            case 8: {
                const wchar_t* rp = (paParams && paParams[0].pwstrVal != nullptr) ? paParams[0].pwstrVal : L"";
                m_FileCount = EnumerateFilesOnDevice(rp);
                size_t len = wcslen(m_FileList) + 1;
                pvarRetValue->pwstrVal = nullptr;
                pvarRetValue->wstrLen = 0;
                if (len > 1 && m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarRetValue->pwstrVal, m_FileList, (uint32_t)len);
                    pvarRetValue->wstrLen = (uint32_t)wcslen(m_FileList);
                }
                else {
                    pvarRetValue->pwstrVal = nullptr;
                    pvarRetValue->wstrLen = 0;
                }
                TV_VT(pvarRetValue) = VTYPE_PWSTR;
                return true;
            }
            default: 
                TV_VT(pvarRetValue) = VTYPE_EMPTY;
                return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (m_iConnect) {
            EXCEPINFO info; ZeroMemory(&info, sizeof(EXCEPINFO));
            info.wCode = ADDIN_E_FAIL; info.bstrSource = SysAllocString(L"ADBFileDriver");
            info.bstrDescription = SysAllocString(L"Ошибка"); info.scode = E_FAIL;
            m_iConnect->AddError(info.wCode, info.bstrSource, info.bstrDescription, info.scode);
            SysFreeString(info.bstrSource); SysFreeString(info.bstrDescription);
        }
        TV_VT(pvarRetValue) = VTYPE_EMPTY;
        return false;
    }
}

void ADBFileDriver::LogWrite(const wchar_t* message) { WriteLog(message); }
void ADBFileDriver::SetLocale(const WCHAR_T*) {}

bool ADBFileDriver::RegisterExtensionAs(WCHAR_T** wsExtName)
{
    if (m_iMemory) {
        size_t len = wcslen(L"ADBFileDriver") + 1;
        m_iMemory->AllocMemory((void**)wsExtName, (unsigned long)(len * sizeof(WCHAR_T)));
        convToShortWchar(wsExtName, L"ADBFileDriver", (uint32_t)len);
    } else { *wsExtName = SysAllocString(L"ADBFileDriver"); }
    return *wsExtName != nullptr;
}

long ADBFileDriver::findName(const wchar_t* names[], const wchar_t* name, const uint32_t size) const
{ for (uint32_t i = 0; i < size; i++) if (wcscmp(names[i], name) == 0) return (long)i; return -1; }

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
static WCHAR_T g_ClassNames[] = L"ADBFileDriver\0";
extern "C" {
const WCHAR_T* GetClassNames() { return g_ClassNames; }
long GetClassObject(const WCHAR_T* clsName, IComponentBase** pIntf)
{
    if (_wcsicmp(clsName, L"ADBFileDriver") == 0) { *pIntf = new ADBFileDriver(); return *pIntf ? 1 : 0; }
    *pIntf = nullptr; return 0;
}
long DestroyObject(IComponentBase** pIntf) { if (pIntf && *pIntf) { delete *pIntf; *pIntf = nullptr; return 0; } return -1; }
AppCapabilities SetPlatformCapabilities(const AppCapabilities capabilities) { return eAppCapabilitiesLast; }
long GetAttachType() { return 0; }
}