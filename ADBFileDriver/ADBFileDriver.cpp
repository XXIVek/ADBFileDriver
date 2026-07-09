// ADBFileDriver.cpp : Реализация компоненты для 1С:Предприятие 8.3
// Android Device Manager - работа с USB устройствами через ADB

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
    L"DownloadFile", L"UploadFile", L"DeleteFile", L"ListNames"
};
static const wchar_t* g_MethodNamesRU[] = { 
    L"ПеречислитьУстройства", L"Подключить", L"Отключить", L"СписокФайлов", 
    L"СкачатьФайл", L"ЗагрузитьФайл", L"УдалитьФайл", L"СписокИменФайлов"
};
#define METHODS_COUNT 8

// ===== Глобальные переменные =====
static wchar_t g_adbPath[512] = L"";
static wchar_t g_LogPathGlobal[512] = L"";
static bool g_LogEnabled = false;

// ===== Буфер для хранения информации инициализации =====
static wchar_t g_InitLogBuffer[4096] = L"";
static bool g_InitLogBufferDirty = false;

static void InitLogBuffer_Add(const wchar_t* msg)
{
    if (msg == nullptr || g_InitLogBuffer[0] == L'\0') {
        if (msg) wcscpy_s(g_InitLogBuffer, 4096, msg);
        g_InitLogBufferDirty = true;
        return;
    }
    size_t currentLen = wcslen(g_InitLogBuffer);
    size_t msgLen = wcslen(msg);
    if (currentLen + msgLen + 2 < 4095) {
        g_InitLogBuffer[currentLen] = L'\n';
        g_InitLogBuffer[currentLen + 1] = L'\0';
        wcscat_s(g_InitLogBuffer + currentLen, 4096 - currentLen, msg);
        g_InitLogBufferDirty = true;
    }
}

static void InitLogBuffer_Clear()
{
    g_InitLogBuffer[0] = L'\0';
    g_InitLogBufferDirty = false;
}

// ===== Forward declarations =====
static bool FindAdbExe(const wchar_t* adbDirectory);
static bool AdbExec(const wchar_t* args, wchar_t* output, DWORD outputSize);
static bool AdbShellList(const wchar_t* serial, const wchar_t* remotePath, wchar_t* fileList, DWORD fileListSize);
static void WriteLog(const wchar_t* msg);

// ===== WriteLog - запись в лог-файл =====
static void WriteLog(const wchar_t* msg)
{
    if (msg == nullptr) return;
    
    OutputDebugStringW(msg);
    OutputDebugStringW(L"\r\n");
    
    // Логирование в файл если включено и путь задан
    // g_LogEnabled — глобальная переменная, синхронизируется с m_EnableLog через SetPropVal
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
    
    char timeStr[32];
    WideCharToMultiByte(CP_ACP, 0, timeBuf, -1, timeStr, 32, NULL, NULL);
    
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, msg, -1, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) { CloseHandle(hLog); return; }
    
    char* utf8Str = new char[utf8Len + 1];
    WideCharToMultiByte(CP_UTF8, 0, msg, -1, utf8Str, utf8Len + 1, NULL, NULL);
    
    char finalEntry[2048];
    strcpy_s(finalEntry, 2048, timeStr);
    strcat_s(finalEntry, 2048, " ");
    strcat_s(finalEntry, 2048, utf8Str);
    strcat_s(finalEntry, 2048, "\r\n");
    
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
    m_AdbDirectory[0] = L'\0';
    m_LastDeviceSerial[0] = L'\0';
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
    { wchar_t msg[512]; swprintf_s(msg, L"[INIT] Факт подключения компоненты: успешно"); InitLogBuffer_Add(msg); }
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
    { WriteLog(L"[DONE] Факт отключения компоненты: начинается"); }
    if (m_LogHandle != nullptr && m_LogHandle != INVALID_HANDLE_VALUE) { CloseHandle((HANDLE)m_LogHandle); m_LogHandle = nullptr; }
    if (m_iConnect) { m_iConnect = nullptr; }
    if (m_iMemory) { m_iMemory = nullptr; }
    m_bInitialized = false;
    { WriteLog(L"[DONE] Факт отключения компоненты: завершено, ADB сервер не завершается (оставлен для других процессов)"); }
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
    tVarInit(pvarPropVal);
    
    try {
        switch (lPropNum) {
            case 0: { // Version
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
            case 7: { // AdbDirectory
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
    } catch (...) {
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
    try {
        switch (lPropNum) {
            case 1: { // EnableLog
                long boolVal = 0;
                if (TV_VT(varPropVal) == VTYPE_BOOL) boolVal = (TV_BOOL(varPropVal) != 0) ? 1 : 0;
                else if (TV_VT(varPropVal) == VTYPE_I4) boolVal = (TV_I4(varPropVal) != 0) ? 1 : 0;
                m_EnableLog = (boolVal != 0);
                wcscpy_s(g_LogPathGlobal, 512, m_LogPath);
                g_LogEnabled = m_EnableLog;
                { wchar_t msg[512]; swprintf_s(msg, L"[SET] Свойство EnableLog = %s", m_EnableLog ? L"true" : L"false"); InitLogBuffer_Add(msg); }
                return true;
            }
            case 2: { // LogPath
                if (TV_VT(varPropVal) == VTYPE_PWSTR && varPropVal->pwstrVal != nullptr) {
                    size_t len = wcslen(varPropVal->pwstrVal);
                    if (len > 0 && len < 511) {
                        wcscpy_s(m_LogPath, 512, varPropVal->pwstrVal);
                        wcscpy_s(g_LogPathGlobal, 512, m_LogPath);
                        g_LogEnabled = m_EnableLog;
                        { wchar_t msg[512]; swprintf_s(msg, L"[SET] Свойство LogPath = %s", varPropVal->pwstrVal); InitLogBuffer_Add(msg); }
                    }
                }
                return true;
            }
            case 7: { // AdbDirectory
                if (TV_VT(varPropVal) == VTYPE_PWSTR && varPropVal->pwstrVal != nullptr) {
                    size_t len = wcslen(varPropVal->pwstrVal);
                    if (len > 0 && len < 511) {
                        wcscpy_s(m_AdbDirectory, 512, varPropVal->pwstrVal);
                        { wchar_t msg[512]; swprintf_s(msg, L"[SET] Свойство КаталогADB = %s", varPropVal->pwstrVal); InitLogBuffer_Add(msg); }
                    }
                }
                return true;
            }
            default:
                return false;
        }
    } catch (...) {
        return false;
    }
}

bool ADBFileDriver::IsPropReadable(const long lPropNum) { return lPropNum >= 0 && lPropNum < PROPS_COUNT; }
bool ADBFileDriver::IsPropWritable(const long lPropNum) { return lPropNum == 1 || lPropNum == 2 || lPropNum == 7; }

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
    case 4: return 1; case 5: return 2; case 6: return 1; case 7: return 1;
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

static bool FindAdbExe(const wchar_t* adbDirectory)
{
    if (g_adbPath[0] != L'\0') return true;
    
    if (adbDirectory != nullptr && adbDirectory[0] != L'\0') {
        wchar_t adbPath[1024];
        wcscpy_s(adbPath, 1024, adbDirectory);
        size_t dirLen = wcslen(adbPath);
        if (dirLen > 0 && adbPath[dirLen - 1] != L'\\' && adbPath[dirLen - 1] != L'/') {
            wcscat_s(adbPath, 1024, L"\\");
        }
        wcscat_s(adbPath, 1024, L"adb.exe");
        
        if (PathFileExistsW(adbPath)) {
            wcscpy_s(g_adbPath, 512, adbPath);
            return true;
        }
    }
    
    wchar_t pathEnv[1024];
    DWORD ret = GetEnvironmentVariableW(L"PATH", pathEnv, 1024);
    if (ret > 0 && ret < 1024) {
        wchar_t* copy = new (std::nothrow) wchar_t[1024];
        if (copy) {
            wcscpy_s(copy, 1024, pathEnv);
            wchar_t* tok = wcstok_s(copy, L";;", nullptr);
            while (tok) {
                wchar_t full[1024]; swprintf_s(full, 1024, L"%s\\\\adb.exe", tok);
                if (PathFileExistsW(full)) {
                    wcscpy_s(g_adbPath, 512, full);
                    delete[] copy;
                    return true;
                }
                tok = wcstok_s(nullptr, L";;", nullptr);
            }
            delete[] copy;
        }
    }
    
    return false;
}

static bool AdbExec(const wchar_t* args, wchar_t* output, DWORD outputSize)
{
    if (!FindAdbExe(g_adbPath)) return false;
    
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    
    HANDLE hErrRead, hErrWrite;
    if (!CreatePipe(&hErrRead, &hErrWrite, &sa, 0)) {
        CloseHandle(hRead); CloseHandle(hWrite); return false;
    }
    
    HANDLE hStdInput = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hStdInput == INVALID_HANDLE_VALUE || hStdInput == NULL) {
        CloseHandle(hRead); CloseHandle(hWrite); CloseHandle(hErrRead); CloseHandle(hErrWrite); return false;
    }
    
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {0};
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hErrWrite;
    si.hStdInput = hStdInput;
    
    char adbPathA[1024];
    int pathLen = WideCharToMultiByte(CP_ACP, 0, g_adbPath, -1, adbPathA, sizeof(adbPathA), nullptr, nullptr);
    if (pathLen <= 0) { CloseHandle(hRead); CloseHandle(hWrite); CloseHandle(hErrRead); CloseHandle(hErrWrite); CloseHandle(hStdInput); return false; }
    
    char argsA[2048];
    int argsLen = WideCharToMultiByte(CP_ACP, 0, args, -1, argsA, sizeof(argsA), nullptr, nullptr);
    if (argsLen <= 0) { CloseHandle(hRead); CloseHandle(hWrite); CloseHandle(hErrRead); CloseHandle(hErrWrite); CloseHandle(hStdInput); return false; }
    
    char cmdA[4096];
    if (strchr(adbPathA, ' ') != nullptr) {
        sprintf_s(cmdA, sizeof(cmdA), "\"%s\" %s", adbPathA, argsA);
    } else {
        sprintf_s(cmdA, sizeof(cmdA), "%s %s", adbPathA, argsA);
    }
    
    if (!CreateProcessA(NULL, cmdA, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(si.hStdInput); CloseHandle(hRead); CloseHandle(hWrite); CloseHandle(hErrRead); CloseHandle(hErrWrite); return false;
    }
    
    CloseHandle(hWrite);
    DWORD waitResult = WaitForSingleObject(pi.hProcess, INFINITE);
    
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        CloseHandle(hRead); CloseHandle(hErrRead);
        return false;
    }
    
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    
    DWORD bytesRead = 0; char rawBuf[65536] = ""; char* ptr = rawBuf;
    DWORD totalRead = 0;
    while (totalRead < (DWORD)(sizeof(rawBuf) - 1)) {
        DWORD peekBytes = 0;
        if (!PeekNamedPipe(hRead, NULL, 0, NULL, &peekBytes, NULL)) break;
        if (peekBytes == 0) break;
        DWORD toRead = (peekBytes < 4096) ? peekBytes : 4096;
        if (!ReadFile(hRead, ptr, toRead, &bytesRead, NULL) || bytesRead == 0) break;
        ptr += bytesRead; totalRead += bytesRead;
    }
    CloseHandle(hRead);
    
    DWORD errBytesRead = 0; char errBuf[4096] = "";
    DWORD totalErrRead = 0;
    CloseHandle(hErrRead);
    
    if (output && outputSize > 0 && totalRead > 0) {
        int wLen = MultiByteToWideChar(CP_UTF8, 0, rawBuf, (int)totalRead, nullptr, 0);
        if (wLen > 0) {
            wchar_t* wBuf = new wchar_t[wLen + 1];
            MultiByteToWideChar(CP_UTF8, 0, rawBuf, (int)totalRead, wBuf, wLen);
            wBuf[wLen] = L'\0';
            DWORD maxC = outputSize / sizeof(wchar_t) - 1;
            DWORD outLen = (DWORD)wcslen(wBuf);
            if (outLen > maxC) outLen = maxC;
            wcsncpy_s(output, outputSize, wBuf, outLen);
            output[outLen] = L'\0';
            delete[] wBuf;
        } else {
            output[0] = L'\0';
        }
    } else if (output && outputSize > 0) {
        output[0] = L'\0';
    }
    return true;
}

static void InitLogBuffer_Output()
{
    if (g_InitLogBufferDirty && g_InitLogBuffer[0] != L'\0') {
        WriteLog(L"===== Инициализация =====");
        WriteLog(g_InitLogBuffer);
        WriteLog(L"===== Конец инициализации =====");
        InitLogBuffer_Clear();
    }
}

static bool AdbShellList(const wchar_t* serial, const wchar_t* remotePath, wchar_t* fileList, DWORD fileListSize)
{
    wchar_t cmd[1024];
    
    if (remotePath == nullptr || remotePath[0] == L'\0') {
        if (serial && serial[0] != L'\0') swprintf_s(cmd, 1024, L"-s %s shell ls -l %s", serial, remotePath ? remotePath : L"/");
        else swprintf_s(cmd, 1024, L"shell ls -l /");
    } else {
        if (remotePath[0] == L'/') {
            if (serial && serial[0] != L'\0') swprintf_s(cmd, 1024, L"-s %s shell ls -l %s", serial, remotePath);
            else swprintf_s(cmd, 1024, L"shell ls -l %s", remotePath);
        } else {
            return false;
        }
    }
    
    wchar_t output[65536] = L"";
    if (!AdbExec(cmd, output, sizeof(output))) return false;
    
    fileList[0] = L'\0'; DWORD fc = 0;
    
    wchar_t* normalized = new wchar_t[wcslen(output) * 2 + 1];
    wchar_t* dst = normalized;
    const wchar_t* src = output;
    while (*src != L'\0') {
        if (*src == L'\n') {
            if (src == output || (*(src - 1)) != L'\r') { *dst++ = L'\r'; }
            *dst++ = L'\n';
        }
        else if (*src != L'\r') { *dst++ = *src; }
        src++;
    }
    *dst = L'\0';
    
    wchar_t* lineStart = normalized;
    wchar_t* lineEnd;
    while ((lineEnd = wcschr(lineStart, L'\n')) != nullptr) {
        if (lineEnd > lineStart && *(lineEnd - 1) == L'\r') {
            lineEnd--;
        }
        *lineEnd = L'\0';
        
        if (lineStart[0] != L'\0' && wcsstr(lineStart, L"List of devices") == nullptr) {
            if (_wcsnicmp(lineStart, L"total", 5) != 0) {
                bool isFolder = (lineStart[0] == L'd');
                wchar_t* fileNameStart = lineStart;
                
                for (int pass = 0; pass < 7; pass++) {
                    while (*fileNameStart != L'\0' && *fileNameStart != L' ') fileNameStart++;
                    while (*fileNameStart != L'\0' && (*fileNameStart == L' ' || *fileNameStart == L'\t')) fileNameStart++;
                }
                
                if (fc > 0) wcscat_s(fileList, fileListSize, L",");
                wcscat_s(fileList, fileListSize, L"{\"Name\":\"");
                
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
        }
        lineStart = lineEnd + 1;
    }
    
    delete[] normalized;
    return fc > 0;
}

uint32_t ADBFileDriver::EnumerateMtpDevices()
{
    m_DeviceCount = 0; m_DeviceList[0] = L'\0'; m_CurrentPath[0] = L'\0';
    
    { InitLogBuffer_Output(); }
    { WriteLog(L"[METHOD] Выполнение метода ПеречислитьУстройства()"); }
    
    if (!FindAdbExe(m_AdbDirectory)) {
        wcscpy_s(m_Status, 512, L"Ошибка: adb.exe не найден");
        wcscpy_s(m_DeviceList, 8192, L"[]");
        { WriteLog(L"[METHOD] ПеречислитьУстройства() результат=Ошибка: adb.exe не найден"); }
        return 0;
    }
    
    wchar_t adbDevices[65536] = L"";
    if (!AdbExec(L"devices -l", adbDevices, sizeof(adbDevices))) {
        wcscpy_s(m_Status, 512, L"При запуске ADB возникла ошибка");
        wcscpy_s(m_DeviceList, 8192, L"[]");
        { WriteLog(L"[METHOD] ПеречислитьУстройства() результат=Ошибка: ADB не ответил"); }
        return 0;
    }
    
    struct AdbDeviceEntry {
        wchar_t serial[256];
        wchar_t product[256];
        wchar_t model[256];
    };
    AdbDeviceEntry adbEntries[256];
    int adbDeviceCount = 0;
    
    wchar_t* adbCopy = new wchar_t[wcslen(adbDevices) + 1];
    wcscpy_s(adbCopy, wcslen(adbDevices) + 1, adbDevices);
    
    wchar_t* normalized = new wchar_t[wcslen(adbCopy) * 2 + 1];
    wchar_t* dst = normalized;
    const wchar_t* src = adbCopy;
    while (*src != L'\0') {
        if (*src == L'\n') { *dst++ = L'\r'; *dst++ = L'\n'; }
        else *dst++ = *src;
        src++;
    }
    *dst = L'\0';
    delete[] adbCopy;
    
    wchar_t* savePtr = nullptr;
    wchar_t* line = wcstok_s(normalized, L"\r\n", &savePtr);
    while (line != nullptr) {
        if (wcsstr(line, L"List of devices") != nullptr || line[0] == L'\0') {
            line = wcstok_s(nullptr, L"\r\n", &savePtr);
            continue;
        }
        
        wchar_t serial[256] = L"";
        wchar_t product[256] = L"";
        wchar_t model[256] = L"";
        
        {
            const wchar_t* start = line;
            const wchar_t* end = line;
            while (*end != L'\0' && (*end == L' ' || *end == L'\t')) end++;
            const wchar_t* serialStart = end;
            while (*end != L'\0' && *end != L' ' && *end != L'\t' && *end != L'\r' && *end != L'\n') end++;
            size_t serialLen = (size_t)(end - serialStart);
            if (serialLen > 0 && serialLen < 256) {
                wcsncpy_s(serial, 256, serialStart, serialLen);
            }
        }
        
        const wchar_t* productPos = wcsstr(line, L"product:");
        if (productPos != nullptr) {
            const wchar_t* prodVal = productPos + 8;
            const wchar_t* prodEnd = wcschr(prodVal, L' ');
            if (prodEnd != nullptr) {
                size_t prodLen = (size_t)(prodEnd - prodVal);
                if (prodLen > 0 && prodLen < 256) wcsncpy_s(product, 256, prodVal, prodLen);
            } else {
                wcscpy_s(product, 256, prodVal);
            }
        }
        
        const wchar_t* modelPos = wcsstr(line, L"model:");
        if (modelPos != nullptr) {
            const wchar_t* modelVal = modelPos + 6;
            const wchar_t* modelEnd = wcschr(modelVal, L' ');
            if (modelEnd != nullptr) {
                size_t modelLen = (size_t)(modelEnd - modelVal);
                if (modelLen > 0 && modelLen < 256) wcsncpy_s(model, 256, modelVal, modelLen);
            } else {
                wcscpy_s(model, 256, modelVal);
            }
        }
        
        bool hasDevice = false;
        const wchar_t* devCheck = wcsstr(line, L" device ");
        if (devCheck != nullptr) {
            hasDevice = true;
        } else {
            size_t lineLen = wcslen(line);
            if (lineLen >= 7 && wcsncmp(line + lineLen - 7, L" device", 6) == 0) {
                hasDevice = true;
            } else {
                devCheck = wcsstr(line, L" device\t");
                if (devCheck != nullptr) {
                    hasDevice = true;
                }
            }
        }
        
        if (hasDevice && serial[0] != L'\0') {
            wcscpy_s(adbEntries[adbDeviceCount].serial, 256, serial);
            wcscpy_s(adbEntries[adbDeviceCount].product, 256, product);
            wcscpy_s(adbEntries[adbDeviceCount].model, 256, model);
            adbDeviceCount++;
        }
        
        line = wcstok_s(nullptr, L"\r\n", &savePtr);
    }
    delete[] normalized;
    
    bool* usbDeviceProcessed = new bool[adbDeviceCount];
    for (int i = 0; i < adbDeviceCount; i++) usbDeviceProcessed[i] = false;
    
    HDEVINFO hDevInfo = SetupDiGetClassDevsW((GUID*)NULL, L"USB", NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hDevInfo != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA devInfo = { sizeof(devInfo) };
        
        for (DWORD i = 0; ; i++) {
            if (!SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo)) break;
            
            wchar_t pnpId[1024] = L"";
            CM_Get_Device_IDW(devInfo.DevInst, pnpId, 1024, 0);
            if (pnpId[0] == L'\0') continue;
            
            wchar_t friendlyName[512] = L"";
            SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_FRIENDLYNAME, NULL, (PBYTE)friendlyName, 512, NULL);
            if (friendlyName[0] == L'\0') SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_DEVICEDESC, NULL, (PBYTE)friendlyName, 512, NULL);
            
            if (!IsAndroidVendorId(pnpId)) continue;
            
            DWORD status, errMsg;
            if (CM_Get_DevNode_Status(&status, &errMsg, devInfo.DevInst, 0) != CR_SUCCESS) continue;
            
            HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
            bool connected = (hKey != INVALID_HANDLE_VALUE);
            if (hKey != INVALID_HANDLE_VALUE) CloseHandle(hKey);
            if (!connected) continue;
            
            wchar_t serialNumber[256] = L"";
            const wchar_t* lastSlash = wcsrchr(pnpId, L'\\');
            if (lastSlash && *(lastSlash + 1) != L'\0') wcscpy_s(serialNumber, 256, lastSlash + 1);
            
            for (int j = 0; j < adbDeviceCount; j++) {
                if (!usbDeviceProcessed[j] && wcsncmp(adbEntries[j].serial, serialNumber, 256) == 0) {
                    usbDeviceProcessed[j] = true;
                    
                    if (m_DeviceCount == 0) wcscpy_s(m_DeviceList, 8192, L"[");
                    else wcscat_s(m_DeviceList, 8192, L",");
                    
                    wchar_t jsonDevice[4096] = L"";
                    wcscat_s(jsonDevice, 4096, L"{\"Serial\":\"");
                    
                    for (size_t si = 0; adbEntries[j].serial[si] != L'\0' && si < 256; si++) {
                        if (adbEntries[j].serial[si] == L'"') wcscat_s(jsonDevice, 4096, L"\\\"");
                        else if (adbEntries[j].serial[si] == L'\\') wcscat_s(jsonDevice, 4096, L"\\\\");
                        else { wchar_t ch[2] = { adbEntries[j].serial[si], L'\0' }; wcscat_s(jsonDevice, 4096, ch); }
                    }
                    wcscat_s(jsonDevice, 4096, L"\",\"Name\":\"");
                    
                    if (adbEntries[j].model[0] != L'\0') {
                        for (size_t si = 0; adbEntries[j].model[si] != L'\0' && si < 256; si++) {
                            if (adbEntries[j].model[si] == L'"') wcscat_s(jsonDevice, 4096, L"\\\"");
                            else if (adbEntries[j].model[si] == L'\\') wcscat_s(jsonDevice, 4096, L"\\\\");
                            else { wchar_t ch[2] = { adbEntries[j].model[si], L'\0' }; wcscat_s(jsonDevice, 4096, ch); }
                        }
                    } else if (friendlyName[0] != L'\0') {
                        for (size_t si = 0; friendlyName[si] != L'\0' && si < 512; si++) {
                            if (friendlyName[si] == L'"') wcscat_s(jsonDevice, 4096, L"\\\"");
                            else if (friendlyName[si] == L'\\') wcscat_s(jsonDevice, 4096, L"\\\\");
                            else { wchar_t ch[2] = { friendlyName[si], L'\0' }; wcscat_s(jsonDevice, 4096, ch); }
                        }
                    } else {
                        wcscat_s(jsonDevice, 4096, L"Unknown");
                    }
                    
                    wcscat_s(jsonDevice, 4096, L"\"}");
                    wcscat_s(m_DeviceList, 8192, jsonDevice);
                    m_DeviceCount++;
                    break;
                }
            }
        }
        
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }
    
    for (int i = 0; i < adbDeviceCount; i++) {
        if (!usbDeviceProcessed[i]) {
            const wchar_t* serial = adbEntries[i].serial;
            
            bool alreadyAdded = false;
            wchar_t searchPattern[512];
            swprintf_s(searchPattern, 512, L"\"Serial\":\"%s\"", serial);
            if (wcsstr(m_DeviceList, searchPattern) != nullptr) {
                alreadyAdded = true;
            }
            
            if (!alreadyAdded) {
                if (m_DeviceCount == 0) wcscpy_s(m_DeviceList, 8192, L"[");
                else wcscat_s(m_DeviceList, 8192, L",");
                
                wchar_t jsonDevice[4096] = L"";
                wcscat_s(jsonDevice, 4096, L"{\"Serial\":\"");
                
                for (size_t si = 0; serial[si] != L'\0' && si < 256; si++) {
                    if (serial[si] == L'"') wcscat_s(jsonDevice, 4096, L"\\\"");
                    else if (serial[si] == L'\\') wcscat_s(jsonDevice, 4096, L"\\\\");
                    else { wchar_t ch[2] = { serial[si], L'\0' }; wcscat_s(jsonDevice, 4096, ch); }
                }
                wcscat_s(jsonDevice, 4096, L"\",\"Name\":\"");
                
                if (adbEntries[i].model[0] != L'\0') {
                    for (size_t si = 0; adbEntries[i].model[si] != L'\0' && si < 256; si++) {
                        if (adbEntries[i].model[si] == L'"') wcscat_s(jsonDevice, 4096, L"\\\"");
                        else if (adbEntries[i].model[si] == L'\\') wcscat_s(jsonDevice, 4096, L"\\\\");
                        else { wchar_t ch[2] = { adbEntries[i].model[si], L'\0' }; wcscat_s(jsonDevice, 4096, ch); }
                    }
                } else {
                    for (size_t si = 0; serial[si] != L'\0' && si < 256; si++) {
                        if (serial[si] == L'"') wcscat_s(jsonDevice, 4096, L"\\\"");
                        else if (serial[si] == L'\\') wcscat_s(jsonDevice, 4096, L"\\\\");
                        else { wchar_t ch[2] = { serial[si], L'\0' }; wcscat_s(jsonDevice, 4096, ch); }
                    }
                }
                
                wcscat_s(jsonDevice, 4096, L"\"}");
                wcscat_s(m_DeviceList, 8192, jsonDevice);
                m_DeviceCount++;
            }
        }
    }
    
    delete[] usbDeviceProcessed;
    
    if (m_DeviceCount > 0) wcscat_s(m_DeviceList, 8192, L"]");
    else wcscpy_s(m_DeviceList, 8192, L"[]");
    
    { wchar_t msg[2048]; swprintf_s(msg, L"[METHOD] ПеречислитьУстройства() результат=%s", m_DeviceList); WriteLog(msg); }
    return m_DeviceCount;
}

bool ADBFileDriver::ConnectToDevice(const wchar_t* deviceName)
{
    { wchar_t msg[512]; swprintf_s(msg, L"[METHOD] Выполнение метода Подключить(%s)", deviceName ? deviceName : L"(null)"); WriteLog(msg); }
    
    if (!FindAdbExe(m_AdbDirectory)) {
        wcscpy_s(m_Status, 512, L"Ошибка: adb.exe не найден");
        { WriteLog(L"[METHOD] Подключить() результат=Ошибка: adb.exe не найден"); }
        return false;
    }
    
    if (m_bConnected) { DisconnectDevice(); }
    
    wchar_t adbDevices[65536] = L"";
    if (!AdbExec(L"devices -l", adbDevices, sizeof(adbDevices))) {
        wcscpy_s(m_Status, 512, L"При запуске ADB возникла ошибка");
        { WriteLog(L"[METHOD] Подключить() результат=Ошибка: ADB не ответил"); }
        return false;
    }
    
    const wchar_t* targetSerial = nullptr;
    
    if (deviceName != nullptr && deviceName[0] != L'\0') {
        wchar_t pattern[512];
        swprintf_s(pattern, 512, L"\"Serial\":\"%s\"", deviceName);
        const wchar_t* matchPos = wcsstr(m_DeviceList, pattern);
        if (matchPos) {
            wcscpy_s(m_AdbSerial, 256, deviceName);
            targetSerial = m_AdbSerial;
        }
    }
    
    if (targetSerial == nullptr) {
        const wchar_t* serialKey = wcsstr(m_DeviceList, L"\"Serial\":\"");
        if (serialKey) {
            const wchar_t* sf = serialKey + 10;
            const wchar_t* se = wcsstr(sf, L"\"");
            if (se) {
                size_t sl = se - sf;
                if (sl > 0 && sl < 256) { wcsncpy_s(m_AdbSerial, 256, sf, sl); m_AdbSerial[sl] = L'\0'; }
                targetSerial = m_AdbSerial;
                wcscpy_s(m_LastDeviceSerial, 256, m_AdbSerial);
            }
        }
    }
    
    if (targetSerial == nullptr || targetSerial[0] == L'\0') {
        wcscpy_s(m_Status, 512, L"Ошибка: ADB-устройство не найдено");
        { WriteLog(L"[METHOD] Подключить() результат=Ошибка: устройство не найдено"); }
        return false;
    }
    
    m_bConnected = true;
    wcscpy_s(m_Status, 512, L"Подключено (ADB)");
    wcscpy_s(m_CurrentPath, 1024, L"");
    
    { wchar_t msg[1024]; swprintf_s(msg, L"[METHOD] Подключить(%s) результат=Успех, подключено к: %s, Status=%s, DeviceCount=%d", deviceName ? deviceName : L"(null)", targetSerial, m_Status, (int)m_DeviceCount); WriteLog(msg); }
    return true;
}

void ADBFileDriver::DisconnectDevice()
{
    m_bConnected = false;
    m_DeviceId[0] = L'\0';
    m_AdbSerial[0] = L'\0';
    m_CurrentPath[0] = L'\0';
    wcscpy_s(m_Status, 512, L"Не подключен");
}

uint32_t ADBFileDriver::EnumerateFilesOnDevice(const wchar_t* remotePath)
{
    m_FileCount = 0; m_FileList[0] = L'\0';
    if (!m_bConnected) { wcscpy_s(m_FileList, 65536, L"[]"); return 0; }
    
    if (remotePath && remotePath[0] != L'\0') wcscpy_s(m_CurrentPath, 1024, remotePath);
    else m_CurrentPath[0] = L'\0';
    
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
    
    if (remotePath && remotePath[0] != L'\0') wcscpy_s(m_CurrentPath, 1024, remotePath);
    else m_CurrentPath[0] = L'\0';
    
    if (!AdbShellList(m_AdbSerial, remotePath ? remotePath : L"/", m_FileList, sizeof(m_FileList))) {
        wcscpy_s(m_FileList, 65536, L"[]"); return 0;
    }
    m_FileCount = 0;
    for (const wchar_t* p = m_FileList; *p; p++) if (*p == L'{') m_FileCount++;
    return m_FileCount;
}

// ===== Helper: convert tVariant to string for logging =====
static void VariantToLogStr(tVariant* var, wchar_t* out, DWORD outSize)
{
    if (var == nullptr || out == nullptr || outSize == 0) { if (out) out[0] = L'\0'; return; }
    out[0] = L'\0';
    if (var->vt == VTYPE_PWSTR && var->pwstrVal != nullptr) {
        DWORD maxLen = outSize / 2 - 1;
        wcscpy_s(out, maxLen + 1, var->pwstrVal);
    } else if (var->vt == VTYPE_BOOL) {
        wcscpy_s(out, outSize, (var->bVal != 0) ? L"true" : L"false");
    } else if (var->vt == VTYPE_I4) {
        swprintf_s(out, outSize / 2, L"%d", (int)var->lVal);
    }
}

bool ADBFileDriver::DownloadFile(const wchar_t* fileName, wchar_t** content, uint32_t* contentSize)
{
    if (!m_bConnected) return false;
    
    wchar_t remotePath[1024];
    if (m_CurrentPath[0] != L'\0') swprintf_s(remotePath, 1024, L"%s/%s", m_CurrentPath, fileName);
    else swprintf_s(remotePath, 1024, L"/%s", fileName);
    
    wchar_t tempPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempPath)) return false;
    wchar_t tempFile[MAX_PATH];
    GetTempFileNameW(tempPath, L"ADB", 0, tempFile);
    
    wchar_t cmd[2048];
    if (m_AdbSerial[0] != L'\0') swprintf_s(cmd, 2048, L"-s %s pull \"%s\" \"%s\"", m_AdbSerial, remotePath, tempFile);
    else swprintf_s(cmd, 2048, L"pull \"%s\" \"%s\"", remotePath, tempFile);
    
    if (!AdbExec(cmd, nullptr, 0)) { DeleteFileW(tempFile); return false; }
    
    HANDLE hFile = CreateFileW(tempFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { DeleteFileW(tempFile); return false; }
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile); DeleteFileW(tempFile);
        if (content) *content = nullptr;
        if (contentSize) *contentSize = 0;
        return true;
    }
    
    char* buffer = new (std::nothrow) char[fileSize + 1];
    if (!buffer) { CloseHandle(hFile); DeleteFileW(tempFile); return false; }
    
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer, fileSize, &bytesRead, NULL)) { delete[] buffer; CloseHandle(hFile); DeleteFileW(tempFile); return false; }
    CloseHandle(hFile);
    
    int wLen = MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, NULL, 0);
    if (wLen <= 0) { delete[] buffer; DeleteFileW(tempFile); return false; }
    
    wchar_t* wBuffer = new (std::nothrow) wchar_t[wLen + 1];
    if (!wBuffer) { delete[] buffer; DeleteFileW(tempFile); return false; }
    
    MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, wBuffer, wLen);
    wBuffer[wLen] = L'\0';
    delete[] buffer;
    
    if (content && contentSize) {
        uint32_t dataLen = (uint32_t)wLen;
        while (dataLen > 0 && (wBuffer[dataLen - 1] == L'\r' || wBuffer[dataLen - 1] == L'\n' || wBuffer[dataLen - 1] == L'\0')) dataLen--;
        
        uint16_t* resultVal = nullptr;
        uint32_t resultLen = 0;
        if (dataLen == 0) {
            if (m_iMemory && m_iMemory->AllocMemory((void**)&resultVal, 2)) {
                resultVal[0] = L'\0';
            }
            resultLen = 0;
        } else if (m_iMemory && m_iMemory->AllocMemory((void**)&resultVal, (unsigned long)((dataLen + 1) * sizeof(WCHAR_T)))) {
            for (uint32_t i = 0; i < dataLen; i++) resultVal[i] = wBuffer[i];
            resultVal[dataLen] = L'\0';
            resultLen = dataLen;
        }
        if (content) *content = (wchar_t*)resultVal;
        if (contentSize) *contentSize = resultLen;
    }
    
    delete[] wBuffer;
    DeleteFileW(tempFile);
    return true;
}

bool ADBFileDriver::UploadFile(const wchar_t* remoteName, const wchar_t* content, uint32_t contentLen)
{
    if (!m_bConnected) return false;
    
    wchar_t tempPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempPath)) return false;
    wchar_t tempFile[MAX_PATH];
    GetTempFileNameW(tempPath, L"ADB", 0, tempFile);
    
    HANDLE hFile = CreateFileW(tempFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, content, contentLen, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) { CloseHandle(hFile); DeleteFileW(tempFile); return false; }
    
    char* utf8Buffer = new (std::nothrow) char[utf8Len + 1];
    if (!utf8Buffer) { CloseHandle(hFile); DeleteFileW(tempFile); return false; }
    
    WideCharToMultiByte(CP_UTF8, 0, content, contentLen, utf8Buffer, utf8Len, NULL, NULL);
    utf8Buffer[utf8Len] = '\0';
    
    DWORD bytesWritten = 0;
    WriteFile(hFile, utf8Buffer, utf8Len, &bytesWritten, NULL);
    CloseHandle(hFile);
    delete[] utf8Buffer;
    
    wchar_t remotePath[1024];
    if (m_CurrentPath[0] != L'\0') swprintf_s(remotePath, 1024, L"%s/%s", m_CurrentPath, remoteName);
    else swprintf_s(remotePath, 1024, L"/%s", remoteName);
    
    wchar_t cmd[2048];
    if (m_AdbSerial[0] != L'\0') swprintf_s(cmd, 2048, L"-s %s push \"%s\" \"%s\"", m_AdbSerial, tempFile, remotePath);
    else swprintf_s(cmd, 2048, L"push \"%s\" \"%s\"", tempFile, remotePath);
    
    bool result = AdbExec(cmd, nullptr, 0);
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
    tVarInit(pvarRetValue);
    
    try {
        switch (lMethodNum) {
            case 0: { // ПеречислитьУстройства
                uint32_t count = EnumerateMtpDevices();
                size_t len = wcslen(m_DeviceList) + 1;
                pvarRetValue->pwstrVal = nullptr;
                pvarRetValue->wstrLen = 0;
                if (len > 1 && m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarRetValue->pwstrVal, m_DeviceList, (uint32_t)len);
                    pvarRetValue->wstrLen = (uint32_t)wcslen(m_DeviceList);
                }
                TV_VT(pvarRetValue) = VTYPE_PWSTR;
                return true;
            }
            case 1: { // Подключить
                const wchar_t* deviceName = (paParams && paParams[0].pwstrVal != nullptr) ? paParams[0].pwstrVal : nullptr;
                bool result = ConnectToDevice(deviceName);
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = result ? VARIANT_TRUE : VARIANT_FALSE;
                return true;
            }
            case 2: { // Отключить
                DisconnectDevice();
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = (VARIANT_TRUE != 0);
                return true;
            }
            case 3: { // СписокФайлов
                const wchar_t* path = (paParams && paParams[0].pwstrVal != nullptr) ? paParams[0].pwstrVal : L"";
                { wchar_t msg[512]; swprintf_s(msg, L"[METHOD] Выполнение метода СписокФайлов(%s)", path); WriteLog(msg); }
                m_FileCount = EnumerateFilesOnDevice(path);
                { wchar_t msg[2048]; swprintf_s(msg, L"[METHOD] СписокФайлов(%s) результат=Успех, FileCount=%d, список=%s", path, (int)m_FileCount, m_FileList); WriteLog(msg); }
                { wchar_t msg2[256]; swprintf_s(msg2, L"[STATUS] FileCount=%d", (int)m_FileCount); WriteLog(msg2); }
                size_t len = wcslen(m_FileList) + 1;
                pvarRetValue->pwstrVal = nullptr;
                pvarRetValue->wstrLen = 0;
                if (len > 1 && m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarRetValue->pwstrVal, m_FileList, (uint32_t)len);
                    pvarRetValue->wstrLen = (uint32_t)wcslen(m_FileList);
                }
                TV_VT(pvarRetValue) = VTYPE_PWSTR;
                return true;
            }
            case 4: { // СкачатьФайл
                const wchar_t* fn = (paParams && paParams[0].pwstrVal != nullptr) ? paParams[0].pwstrVal : L"";
                { wchar_t msg[512]; swprintf_s(msg, L"[METHOD] Выполнение метода СкачатьФайл(%s)", fn); WriteLog(msg); }
                
                wchar_t* fileContent = nullptr;
                uint32_t contentSize = 0;
                bool success = DownloadFile(fn, &fileContent, &contentSize);
                
                pvarRetValue->pwstrVal = nullptr;
                pvarRetValue->wstrLen = 0;
                
                if (success && fileContent != nullptr) {
                    uint32_t dataLen = contentSize - 1;
                    while (dataLen > 0 && (fileContent[dataLen - 1] == L'\r' || fileContent[dataLen - 1] == L'\n' || fileContent[dataLen - 1] == L'\0')) dataLen--;
                    
                    if (dataLen > 0 && m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)((dataLen + 1) * sizeof(WCHAR_T)))) {
                        for (uint32_t i = 0; i < dataLen; i++) pvarRetValue->pwstrVal[i] = fileContent[i];
                        pvarRetValue->pwstrVal[dataLen] = L'\0';
                        pvarRetValue->wstrLen = dataLen;
                    }
                    delete[] fileContent;
                } else {
                    if (m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, 2)) {
                        pvarRetValue->pwstrVal[0] = L'\0';
                    }
                    pvarRetValue->wstrLen = 0;
                }
                
                TV_VT(pvarRetValue) = VTYPE_PWSTR;
                { wchar_t msg[512]; swprintf_s(msg, L"[METHOD] СкачатьФайл(%s) результат=%s", fn, success ? L"Успех" : L"Ошибка"); WriteLog(msg); }
                return true;
            }
            case 5: { // ЗагрузитьФайл
                const wchar_t* rn = (paParams && paParams[0].pwstrVal != nullptr) ? paParams[0].pwstrVal : L"";
                const wchar_t* content = (paParams && paParams[1].vt == VTYPE_PWSTR && paParams[1].pwstrVal != nullptr) ? paParams[1].pwstrVal : L"";
                uint32_t contentLen = (paParams && paParams[1].vt == VTYPE_PWSTR && paParams[1].pwstrVal != nullptr) ? (uint32_t)wcslen(content) : 0;
                { wchar_t msg[512]; swprintf_s(msg, L"[METHOD] Выполнение метода ЗагрузитьФайл(%s)", rn); WriteLog(msg); }
                { wchar_t msg2[1024]; swprintf_s(msg2, L"[METHOD] ЗагрузитьФайл(%s) Содержание=%s", rn, content ? content : L"(null)"); WriteLog(msg2); }
                
                bool success = UploadFile(rn, content, contentLen);
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = success ? VARIANT_TRUE : VARIANT_FALSE;
                { wchar_t msg[512]; swprintf_s(msg, L"[METHOD] ЗагрузитьФайл(%s) результат=%s", rn, success ? L"Успех" : L"Ошибка"); WriteLog(msg); }
                return true;
            }
            case 6: { // УдалитьФайл
                const wchar_t* fn = (paParams && paParams[0].pwstrVal != nullptr) ? paParams[0].pwstrVal : L"";
                { wchar_t msg[512]; swprintf_s(msg, L"[METHOD] Выполнение метода УдалитьФайл(%s)", fn); WriteLog(msg); }
                bool result = DeleteFile(fn);
                TV_VT(pvarRetValue) = VTYPE_BOOL;
                TV_BOOL(pvarRetValue) = result ? VARIANT_TRUE : VARIANT_FALSE;
                { wchar_t msg[512]; swprintf_s(msg, L"[METHOD] УдалитьФайл(%s) результат=%s", fn, result ? L"Успех" : L"Ошибка"); WriteLog(msg); }
                return true;
            }
            case 7: { // СписокИменФайлов
                const wchar_t* rp = (paParams && paParams[0].pwstrVal != nullptr) ? paParams[0].pwstrVal : L"";
                m_FileCount = ListFileNames(rp);
                size_t len = wcslen(m_FileList) + 1;
                pvarRetValue->pwstrVal = nullptr;
                pvarRetValue->wstrLen = 0;
                if (len > 1 && m_iMemory && m_iMemory->AllocMemory((void**)&pvarRetValue->pwstrVal, (unsigned long)(len * sizeof(WCHAR_T)))) {
                    convToShortWchar((WCHAR_T**)&pvarRetValue->pwstrVal, m_FileList, (uint32_t)len);
                    pvarRetValue->wstrLen = (uint32_t)wcslen(m_FileList);
                }
                TV_VT(pvarRetValue) = VTYPE_PWSTR;
                return true;
            }
            default:
                TV_VT(pvarRetValue) = VTYPE_EMPTY;
                return false;
        }
    } catch (...) {
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