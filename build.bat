@echo off
setlocal
echo Building AsciiAquariumWallpaper.exe...
g++ -std=c++17 -O2 -s -Wall -Wextra -mwindows -municode -static -static-libgcc -static-libstdc++ ^
    src/main.cpp src/aquarium.cpp ^
    -o AsciiAquariumWallpaper.exe ^
    -lgdi32 -luser32 -lshell32 -ladvapi32 -lcomctl32 -ld3d11 -ldxgi -ldcomp -lole32

if %errorlevel% neq 0 (
    echo Build FAILED!
    exit /b %errorlevel%
)
echo Build SUCCESS! Output: AsciiAquariumWallpaper.exe
endlocal
