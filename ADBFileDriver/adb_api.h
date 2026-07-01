#pragma once

#include <windows.h>
#include <usb100.h>

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
typedef int bool;
#define true  1
#define false 0
#endif

typedef void* ADBAPIHANDLE;

typedef enum _AdbEndpointType {
    AdbEndpointTypeInvalid = 0,
    AdbEndpointTypeControl,
    AdbEndpointTypeIsochronous,
    AdbEndpointTypeBulk,
    AdbEndpointTypeInterrupt,
} AdbEndpointType;

typedef struct _AdbEndpointInformation {
    unsigned long max_packet_size;
    unsigned long max_transfer_size;
    AdbEndpointType endpoint_type;
    unsigned char endpoint_address;
    unsigned char polling_interval;
    unsigned char setting_index;
} AdbEndpointInformation;

#define ADB_QUERY_BULK_WRITE_ENDPOINT_INDEX  0xFC
#define ADB_QUERY_BULK_READ_ENDPOINT_INDEX  0xFE

// {F72FE0D4-CBCB-407d-8814-9ED673D0DD6B}
#define ANDROID_USB_CLASS_ID \
{0xf72fe0d4, 0xcbcb, 0x407d, {0x88, 0x14, 0x9e, 0xd6, 0x73, 0xd0, 0xdd, 0x6b}}

typedef struct _AdbInterfaceInfo {
    GUID          class_id;
    unsigned long flags;
    wchar_t       device_name[1];
} AdbInterfaceInfo;

typedef enum _AdbOpenAccessType {
    AdbOpenAccessTypeReadWrite,
    AdbOpenAccessTypeRead,
    AdbOpenAccessTypeWrite,
    AdbOpenAccessTypeQueryInfo,
} AdbOpenAccessType;

typedef enum _AdbOpenSharingMode {
    AdbOpenSharingModeReadWrite,
    AdbOpenSharingModeRead,
    AdbOpenSharingModeWrite,
    AdbOpenSharingModeExclusive,
} AdbOpenSharingMode;

EXTERN_C ADBAPIHANDLE __cdecl AdbEnumInterfaces(GUID class_id, bool exclude_not_present, bool exclude_removed, bool active_only);
EXTERN_C bool __cdecl AdbNextInterface(ADBAPIHANDLE adb_handle, AdbInterfaceInfo* info, unsigned long* size);
EXTERN_C bool __cdecl AdbResetInterfaceEnum(ADBAPIHANDLE adb_handle);
EXTERN_C ADBAPIHANDLE __cdecl AdbCreateInterfaceByName(const wchar_t* interface_name);
EXTERN_C bool __cdecl AdbGetSerialNumber(ADBAPIHANDLE adb_interface, void* buffer, unsigned long* buffer_char_size, bool ansi);
EXTERN_C bool __cdecl AdbGetUsbDeviceDescriptor(ADBAPIHANDLE adb_interface, USB_DEVICE_DESCRIPTOR* desc);
EXTERN_C bool __cdecl AdbGetUsbConfigurationDescriptor(ADBAPIHANDLE adb_interface, USB_CONFIGURATION_DESCRIPTOR* desc);
EXTERN_C bool __cdecl AdbGetUsbInterfaceDescriptor(ADBAPIHANDLE adb_interface, USB_INTERFACE_DESCRIPTOR* desc);
EXTERN_C bool __cdecl AdbGetEndpointInformation(ADBAPIHANDLE adb_interface, unsigned char endpoint_index, AdbEndpointInformation* info);
EXTERN_C ADBAPIHANDLE __cdecl AdbOpenEndpoint(ADBAPIHANDLE adb_interface, unsigned char endpoint_index, AdbOpenAccessType access_type, AdbOpenSharingMode sharing_mode);
EXTERN_C ADBAPIHANDLE __cdecl AdbOpenDefaultBulkReadEndpoint(ADBAPIHANDLE adb_interface, AdbOpenAccessType access_type, AdbOpenSharingMode sharing_mode);
EXTERN_C ADBAPIHANDLE __cdecl AdbOpenDefaultBulkWriteEndpoint(ADBAPIHANDLE adb_interface, AdbOpenAccessType access_type, AdbOpenSharingMode sharing_mode);
EXTERN_C ADBAPIHANDLE __cdecl AdbGetEndpointInterface(ADBAPIHANDLE adb_endpoint);
EXTERN_C bool __cdecl AdbQueryInformationEndpoint(ADBAPIHANDLE adb_endpoint, AdbEndpointInformation* info);
EXTERN_C ADBAPIHANDLE __cdecl AdbReadEndpointAsync(ADBAPIHANDLE adb_endpoint, void* buffer, unsigned long bytes_to_read, unsigned long* bytes_read, unsigned long time_out, HANDLE event_handle);
EXTERN_C ADBAPIHANDLE __cdecl AdbWriteEndpointAsync(ADBAPIHANDLE adb_endpoint, void* buffer, unsigned long bytes_to_write, unsigned long* bytes_written, unsigned long time_out, HANDLE event_handle);
EXTERN_C bool __cdecl AdbReadEndpointSync(ADBAPIHANDLE adb_endpoint, void* buffer, unsigned long bytes_to_read, unsigned long* bytes_read, unsigned long time_out);
EXTERN_C bool __cdecl AdbWriteEndpointSync(ADBAPIHANDLE adb_endpoint, void* buffer, unsigned long bytes_to_write, unsigned long* bytes_written, unsigned long time_out);
EXTERN_C bool __cdecl AdbGetOvelappedIoResult(ADBAPIHANDLE adb_io_completion, LPOVERLAPPED overlapped, unsigned long* bytes_transferred, bool wait);
EXTERN_C bool __cdecl AdbHasOvelappedIoComplated(ADBAPIHANDLE adb_io_completion);
EXTERN_C bool __cdecl AdbCloseHandle(ADBAPIHANDLE adb_handle);