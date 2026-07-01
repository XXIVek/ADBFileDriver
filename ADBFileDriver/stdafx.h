#pragma once

// Заголовочные файлы Windows
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

// Заголовочные файлы OLE
#include <ole2.h>
#include <objbase.h>
#include <oleauto.h>

// Заголовочные файлы STL
#include <string>
#include <vector>
#include <algorithm>

// Включаем типы из Native API
#include "types.h"

// Определения для удобства
#define VARIANT_TRUE  ((VARIANT_BOOL)-1)
#define VARIANT_FALSE ((VARIANT_BOOL)0)

// Коды ошибок
#define ADDIN_E_FAIL          1006

// Функции преобразования строк (как в innNative)
inline uint32_t convToShortWchar(WCHAR_T** Dest, const wchar_t* Source, uint32_t len = 0)
{
    if (!len)
        len = static_cast<uint32_t>(wcslen(Source)) + 1;

    if (!*Dest)
        *Dest = new WCHAR_T[len];

    ::memset(*Dest, 0, len * sizeof(WCHAR_T));

    for (uint32_t i = 0; i < len; i++) {
        (*Dest)[i] = static_cast<WCHAR_T>(Source[i]);
    }

    return len;
}

inline uint32_t convFromShortWchar(wchar_t** Dest, const WCHAR_T* Source, uint32_t len = 0)
{
    if (!len) {
        len = static_cast<uint32_t>(wcslen(static_cast<const wchar_t*>(Source))) + 1;
    }

    if (!*Dest)
        *Dest = new wchar_t[len];

    ::memset(*Dest, 0, len * sizeof(wchar_t));

    for (uint32_t i = 0; i < len; i++) {
        (*Dest)[i] = static_cast<wchar_t>(Source[i]);
    }

    return len;
}