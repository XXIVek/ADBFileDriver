#pragma once

#include "ComponentBase.h"
#include "AddInDefBase.h"
#include "IMemoryManager.h"
#include "stdafx.h"

#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <string>
#include <locale>
#include <fstream>
#include <mutex>

///////////////////////////////////////////////////////////////////////////////
// class ADBFileDriver - Android Device Manager через ADB
class ADBFileDriver : public IComponentBase
{
public:
      enum Props {
          epVersion, epEnableLog, epLogPath, epStatus,
           epDeviceCount, epFileCount, epCurrentCatalog, epLast,
           epAdbDirectory
      };

     enum Methods {
         emEnumerateDevices, emConnect, emDisconnect, emListFiles,
         emDownloadFile, emUploadFile, emDeleteFile, emListNames,
         emListFilesRecursive
     };

     ADBFileDriver(void);
     virtual ~ADBFileDriver();

     virtual bool ADDIN_API Init(void*);
     virtual bool ADDIN_API setMemManager(void* mem);
     virtual long ADDIN_API GetInfo();
     virtual void ADDIN_API Done();
     virtual long ADDIN_API GetNProps();
     virtual long ADDIN_API FindProp(const WCHAR_T* wsPropName);
     virtual const WCHAR_T* ADDIN_API GetPropName(long lPropNum, long lPropAlias);
     virtual bool ADDIN_API GetPropVal(const long lPropNum, tVariant* pvarPropVal);
     virtual bool ADDIN_API SetPropVal(const long lPropNum, tVariant* varPropVal);
     virtual bool ADDIN_API IsPropReadable(const long lPropNum);
     virtual bool ADDIN_API IsPropWritable(const long lPropNum);
     virtual long ADDIN_API GetNMethods();
     virtual long ADDIN_API FindMethod(const WCHAR_T* wsMethodName);
     virtual const WCHAR_T* ADDIN_API GetMethodName(const long lMethodNum, const long lMethodAlias);
     virtual long ADDIN_API GetNParams(const long lMethodNum);
     virtual bool ADDIN_API GetParamDefValue(const long lMethodNum, const long lParamNum, tVariant *pvarParamDefValue);
     virtual bool ADDIN_API HasRetVal(const long lMethodNum);
     virtual bool ADDIN_API CallAsProc(const long lMethodNum, tVariant* paParams, const long lSizeArray);
     virtual bool ADDIN_API CallAsFunc(const long lMethodNum, tVariant* pvarRetValue, tVariant* paParams, const long lSizeArray);
     virtual void ADDIN_API SetLocale(const WCHAR_T* loc);
     virtual bool ADDIN_API RegisterExtensionAs(WCHAR_T**);
     void LogWrite(const wchar_t* message);

private:
     long findName(const wchar_t* names[], const wchar_t* name, const uint32_t size) const;
     void addError(uint32_t wcode, const wchar_t* source, const wchar_t* descriptor, long code);

     IAddInDefBase      *m_iConnect;
     IMemoryManager     *m_iMemory;
     bool                m_bInitialized;
     bool                m_EnableLog;
     wchar_t             m_LogPath[512];
     void*               m_LogHandle;
     CRITICAL_SECTION    m_LogLock;
     bool                m_bConnected;
     wchar_t             m_Status[512];
      wchar_t             m_DeviceList[8192];
      uint32_t            m_DeviceCount;
      wchar_t             m_LastDeviceSerial[256];  // Serial первого устройства для подключения
      wchar_t             m_DeviceId[512];
     wchar_t             m_AdbSerial[256];       // ADB serial устройства
     wchar_t             m_CurrentPath[1024];
     wchar_t             m_FileList[65536];
     uint32_t            m_FileCount;
      wchar_t             m_AdbDirectory[1024];  // Каталог расположения ADB

     uint32_t EnumerateMtpDevices();
     bool ConnectToDevice(const wchar_t* deviceName);
     void DisconnectDevice();
     uint32_t EnumerateFilesOnDevice(const wchar_t* remotePath);
     uint32_t ListFileNames(const wchar_t* remotePath);
      bool DownloadFile(const wchar_t* fileName, wchar_t** content, uint32_t* contentSize);
      bool UploadFile(const wchar_t* remoteName, const wchar_t* content, uint32_t contentLen);
     bool DeleteFile(const wchar_t* fileName);
     bool FindFileByName(const wchar_t* fileName, const wchar_t* localPath);
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