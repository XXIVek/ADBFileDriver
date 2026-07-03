# ADBFileDriver Build Script for Windows (x64)

Write-Host "=== Build ADBFileDriver (Win64) ==="
Write-Host ""

# Check CMake
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Host "ERROR: CMake not found. Install CMake 3.10+."
    exit 1
}
Write-Host "CMake found."

# Create build folder
if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
    Write-Host "Created build/ folder."
}

# Generate project
Write-Host "Generating project..."
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Project generation failed."
    exit 1
}
Write-Host "Project generated."

# Build
$config = if ($args -contains "Debug") { "Debug" } else { "Release" }
Write-Host "Building ($config)..."
cmake --build . --config $config
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed."
    exit 1
}
Write-Host "Build completed."

# Copy MANIFEST.xml
if (-not (Test-Path "..\1C\Release\Win64\bundle")) {
    New-Item -ItemType Directory -Path "..\1C\Release\Win64\bundle" | Out-Null
}
Copy-Item "..\bundle\MANIFEST.xml" -Destination "..\1C\Release\Win64\bundle\" -Force

Write-Host ""
Write-Host "=== Build Success! ==="
Write-Host "DLL:  1C/Release/Win64/ADBFileDriver_Win64.dll"
Write-Host "Manifest: 1C/Release/Win64/bundle/MANIFEST.xml"
Write-Host ""
Write-Host "Usage: .\build.ps1 [Debug|Release]"
Write-Host ""
Write-Host "Usage in 1C:"
Write-Host "  Driver = New('AddIn.ADBFileDriver.ADBFileDriver')"
Write-Host "  Message(Driver.Version);"
Write-Host "  Message(Driver.EnableLog);"
Write-Host "  Message(Driver.LogPath);"
Write-Host ""