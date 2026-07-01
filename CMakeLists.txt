cmake_minimum_required(VERSION 3.10)
project(ADBFileDriver VERSION 1.0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Настройка путей
set(CMAKE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(SRC_DIR ${CMAKE_SOURCE_DIR}/ADBFileDriver)

# Исходные файлы
set(SOURCES
    ${SRC_DIR}/ADBFileDriver.cpp
)

# Включаемые директории
include_directories(${INCLUDE_DIR})

# Win64 сборка
add_library(ADBFileDriver_Win64 SHARED ${SOURCES})

# DEF файл для экспорта
target_sources(ADBFileDriver_Win64 PRIVATE ${SRC_DIR}/ADBFileDriver.def)

# Настройка выходных файлов
set_target_properties(ADBFileDriver_Win64 PROPERTIES
    OUTPUT_NAME ADBFileDriver_Win64
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/1C/Release/Win64"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/1C/Release/Win64"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/1C/Release/Win64"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/1C/Release/Win64"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/1C/Release/Win64"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/1C/Release/Win64"
)

# Компилятор Windows
if(MSVC)
    target_compile_options(ADBFileDriver_Win64 PRIVATE /utf-8 /EHsc)
    target_link_libraries(ADBFileDriver_Win64 ole32.lib oleaut32.lib shlwapi.lib advapi32.lib)
endif()