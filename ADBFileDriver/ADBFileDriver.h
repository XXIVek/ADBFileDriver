#pragma once

#include "ComponentBase.h"
#include "AddInDefBase.h"
#include "IMemoryManager.h"
#include "stdafx.h"

#include <windows.h>
#include <propvarutil.h>
#include <objbase.h>
#include <stdio.h>
#include <wchar.h>
#include <string>
#include <locale>
#include <fstream>
#include <mutex>

// Макрос для отладочного вывода через DebugView
#define DEBUG_LOG(msg) do { OutputDebugStringW(msg); OutputDebugStringW(L"\n"); } while(0)
#define DEBUG_LOG_FMT(fmt, ...) do { wchar_t _dbgbuf[512]; swprintf_s(_dbgbuf, fmt, __VA_ARGS__); OutputDebugStringW(_dbgbuf); OutputDebugStringW(L"\n"); } while(0)

// GUID для IPortableDeviceManager (из portabledeviceapi.h)
// CLSID_PortableDeviceManager
static const CLSID CLSID_PortableDeviceManager = 
{ 0x72A2E8D8, 0x1F2, 0x49F, { 0xB3, 0x1C, 0x56, 0x5B, 0x3F, 0x6D, 0x4F, 0x5A } };

// IID_IPortableDeviceManager - правильный GUID из portabledevicemanager.h
static const IID IID_IPortableDeviceManager = 
{ 0x3D2CDE48, 0x5B, 0x4D, { 0x9D, 0x35, 0xF7, 0xE4, 0x9B, 0xB1, 0x3B, 0xD2 } };

// Объявление интерфейса IPortableDeviceManager (так как заголовок недоступен)
struct IPortableDeviceManager : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDevices(
        _Out_writes_(c32Devices) LPWSTR *ppwstrDeviceIds,
        _In_ DWORD c32Devices,
        _Out_ DWORD *pc32DevicesReturned) = 0;
    
    virtual HRESULT STDMETHODCALLTYPE GetDeviceType(
        _In_ LPCWSTR pwszDeviceId,
        _Out_writes_(c32Type) LPWSTR pwszType,
        _Inout_ DWORD *pc32Type) = 0;
};

///////////////////////////////////////////////////////////////////////////////
// class ADBFileDriver (MTP Device Manager)
class ADBFileDriver : public IComponentBase
{
public:
    enum Props
    {
        epVersion,
        epEnableLog,
        epLogPath,
        epStatus,
        epDeviceCount,
        epLast      
    };

    enum Methods
    {
        emEnumerateDevices,
        emConnect,
        emDisconnect,
        emLast      
    };

    ADBFileDriver(void);
    virtual ~ADBFileDriver();

    // IInitDoneBase
    virtual bool ADDIN_API Init(void*);
    virtual bool ADDIN_API setMemManager(void* mem);
    virtual long ADDIN_API GetInfo();
    virtual void ADDIN_API Done();

    // ILanguageExtenderBase
    virtual long ADDIN_API GetNProps();
    virtual long ADDIN_API FindProp(const WCHAR_T* wsPropName);
    virtual const WCHAR_T* ADDIN_API GetPropName(long lPropNum, long lPropAlias);
    virtual bool ADDIN_API GetPropVal(const long lPropNum, tVariant* pvarPropVal);
    virtual bool ADDIN_API SetPropVal(const long lPropNum, tVariant* varPropVal);
    virtual bool ADDIN_API IsPropReadable(const long lPropNum);
    virtual bool ADDIN_API IsPropWritable(const long lPropNum);

    virtual long ADDIN_API GetNMethods();
    virtual long ADDIN_API FindMethod(const WCHAR_T* wsMethodName);
    virtual const WCHAR_T* ADDIN_API GetMethodName(const long lMethodNum, 
                            const long lMethodAlias);
    virtual long ADDIN_API GetNParams(const long lMethodNum);
    virtual bool ADDIN_API GetParamDefValue(const long lMethodNum, const long lParamNum,
                            tVariant *pvarParamDefValue);   
    virtual bool ADDIN_API HasRetVal(const long lMethodNum);
    virtual bool ADDIN_API CallAsProc(const long lMethodNum,
                    tVariant* paParams, const long lSizeArray);
    virtual bool ADDIN_API CallAsFunc(const long lMethodNum,
                tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray);

    // LocaleBase
    virtual void ADDIN_API SetLocale(const WCHAR_T* loc);
    virtual bool ADDIN_API RegisterExtensionAs(WCHAR_T**);

    // Логирование
    void LogWrite(const wchar_t* message);

private:
    long findName(const wchar_t* names[], const wchar_t* name, const uint32_t size) const;
    void addError(uint32_t wcode, const wchar_t* source, const wchar_t* descriptor, long code);

    // Attributes
    IAddInDefBase      *m_iConnect;
    IMemoryManager     *m_iMemory;
    bool                m_bInitialized;

    // Параметры логирования
    bool                m_EnableLog;
    wchar_t             m_LogPath[512];
    void*               m_LogHandle;  // HANDLE instead of std::ofstream
    CRITICAL_SECTION    m_LogLock;

    // Состояние подключения
    bool                m_bConnected;
    wchar_t             m_Status[512];
    
    // MTP устройства
    wchar_t             m_DeviceList[8192];  // JSON строка с устройствами
    uint32_t            m_DeviceCount;
    
    // MTP интерфейс устройства
    void*               m_pDevice;  // IPortableDevice*
    wchar_t             m_DeviceId[512];   // ID подключенного устройства

    // Вспомогательные функции для MTP
    uint32_t EnumerateMtpDevices();
    bool ConnectToDevice(const wchar_t* deviceName);
    void DisconnectDevice();
};

class WcharWrapper
{
public:
    WcharWrapper(const wchar_t* str);
    ~WcharWrapper();
    operator const wchar_t*(){ return m_str_wchar; }
    operator wchar_t*(){ return m_str_wchar; }
private:
    WcharWrapper& operator = (const WcharWrapper& other) { return *this; }
    WcharWrapper(const WcharWrapper& other) { }
private:
    wchar_t* m_str_wchar;
};