cmake_minimum_required(VERSION 3.10)
project(ADBFileDriver VERSION 1.0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ========== НАСТРОЙКА ПУТЕЙ ==========
set(CMAKE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(SRC_DIR ${CMAKE_SOURCE_DIR}/ADBFileDriver)
set(BUNDLE_DIR ${CMAKE_SOURCE_DIR}/bundle)
set(OUTPUT_DIR_1C ${CMAKE_SOURCE_DIR}/1C/Release)

# ========== ИСХОДНЫЕ ФАЙЛЫ ==========
set(SOURCES
    ${SRC_DIR}/ADBFileDriver.cpp
)

set(DEF_FILE
    ${SRC_DIR}/ADBFileDriver.def
)

set(HEADERS
    ${SRC_DIR}/ADBFileDriver.h
    ${SRC_DIR}/stdafx.h
)

# ========== ВКЛЮЧАЕМЫЕ ДИРЕКТОРИИ ==========
include_directories(${INCLUDE_DIR})

# ========== WIN64 СБОРКА ==========
add_library(ADBFileDriver_Win64 SHARED ${SOURCES} ${DEF_FILE} ${HEADERS})

# DEF файл для экспорта функций
target_sources(ADBFileDriver_Win64 PRIVATE ${SRC_DIR}/ADBFileDriver.def)

# Свойства таргета Win64
set_target_properties(ADBFileDriver_Win64 PROPERTIES
    OUTPUT_NAME ADBFileDriver_Win64
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIR_1C}/Win64"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIR_1C}/Win64"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIR_1C}/Win64"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIR_1C}/Win64"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIR_1C}/Win64"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIR_1C}/Win64"
)

# ========== WIN32 СБОРКА ==========
add_library(ADBFileDriver_Win32 SHARED ${SOURCES} ${DEF_FILE} ${HEADERS})

# DEF файл для экспорта функций
target_sources(ADBFileDriver_Win32 PRIVATE ${SRC_DIR}/ADBFileDriver.def)

# Свойства таргета Win32
set_target_properties(ADBFileDriver_Win32 PROPERTIES
    OUTPUT_NAME ADBFileDriver_Win32
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIR_1C}/Win32"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIR_1C}/Win32"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIR_1C}/Win32"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIR_1C}/Win32"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIR_1C}/Win32"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIR_1C}/Win32"
)

# ========== MSVC НАСТРОЙКИ ==========
if(MSVC)
    # Кодировка источника
    target_compile_options(ADBFileDriver_Win64 PRIVATE /utf-8 /EHsc)
    target_compile_options(ADBFileDriver_Win32 PRIVATE /utf-8 /EHsc)

    # Библиотеки для линковки
    target_link_libraries(ADBFileDriver_Win64 ole32.lib oleaut32.lib shlwapi.lib shell32.lib advapi32.lib comctl32.lib)
    target_link_libraries(ADBFileDriver_Win32 ole32.lib oleaut32.lib shlwapi.lib shell32.lib advapi32.lib comctl32.lib)

    # Оптимизация для Release
    target_compile_options(ADBFileDriver_Win64 PRIVATE /O2)
    target_compile_options(ADBFileDriver_Win32 PRIVATE /O2)
    
    # Debug настройки
    target_compile_options(ADBFileDriver_Win64 PRIVATE $<$<CONFIG:DEBUG>:/Od /Zi /D_DEBUG>)
    target_compile_options(ADBFileDriver_Win32 PRIVATE $<$<CONFIG:DEBUG>:/Od /Zi /D_DEBUG>)
    target_link_options(ADBFileDriver_Win64 PRIVATE $<$<CONFIG:DEBUG>:/DEBUG>)
    target_link_options(ADBFileDriver_Win32 PRIVATE $<$<CONFIG:DEBUG>:/DEBUG>)
endif()

# ========== MANIFEST.XML КОПИРОВАНИЕ ==========
# Создание папки bundle
file(MAKE_DIRECTORY "${OUTPUT_DIR_1C}/bundle")

# Копирование MANIFEST.xml после сборки
add_custom_command(TARGET ADBFileDriver_Win64 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BUNDLE_DIR}/MANIFEST.xml"
        "${OUTPUT_DIR_1C}/bundle/MANIFEST.xml"
    COMMENT "Copying MANIFEST.xml to output..."
)

# ========== БАНЛЕР / ПАКЕТНАЯ СБОРКА ==========
add_custom_target(CopyBundle ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR_1C}/bundle"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BUNDLE_DIR}/MANIFEST.xml"
        "${OUTPUT_DIR_1C}/bundle/MANIFEST.xml"
    COMMENT "Preparing bundle directory..."
)

add_dependencies(CopyBundle ADBFileDriver_Win64 ADBFileDriver_Win32)

# ========== ПОМЕЩЕНИЕ В ПОМНИТЬ ==========
message(STATUS "=======================================")
message(STATUS "  ADBFileDriver Build Configuration")
message(STATUS "=======================================")
message(STATUS "  Project: ${PROJECT_NAME}")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Output:  ${OUTPUT_DIR_1C}")
message(STATUS "  Bundle:  ${OUTPUT_DIR_1C}/bundle/")
message(STATUS "=======================================")