@echo off
REM =============================================
REM Регистрация COM-классов из PortableDeviceApi.dll
REM Запуск от имени администратора!
REM =============================================

echo.
echo =============================================
echo  Регистрация PortableDeviceApi.dll
echo =============================================
echo.

REM Проверка прав администратора
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [ОШИБКА] Необходим запуск от имени администратора!
    echo.
    echo Правый клик на файле -^> "Запуск от имени администратора"
    echo.
    pause
    exit /b 1
)

REM Регистрация 64-bit версии
echo [1/2] Регистрация 64-bit версии...
regsvr32 /s "C:\Windows\SysWOW64\PortableDeviceApi.dll"
if %errorLevel% equ 0 (
    echo       OK: C:\Windows\SysWOW64\PortableDeviceApi.dll
) else (
    echo       ERROR: Код ошибки %errorLevel%
)

REM Регистрация 32-bit версии
echo.
echo [2/2] Регистрация 32-bit версии...
regsvr32 /s "C:\Windows\System32\PortableDeviceApi.dll"
if %errorLevel% equ 0 (
    echo       OK: C:\Windows\System32\PortableDeviceApi.dll
) else (
    echo       ERROR: Код ошибки %errorLevel%
)

echo.
echo =============================================
echo  Регистрация завершена
echo =============================================
echo.
pause