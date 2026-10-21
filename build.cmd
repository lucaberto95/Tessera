@echo off
rem Builds Tessera with MSVC + Ninja. Usage: build.cmd [Release|Debug]
rem Extra CMake arguments can be passed in the TESSERA_CMAKE_ARGS environment variable.
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release
if not defined QTDIR set QTDIR=C:\Qt\6.10.3\msvc2022_64
set VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
set CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set NINJA=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set BUILD=%~dp0build\%CONFIG%
"%CMAKE%" -S "%~dp0." -B "%BUILD%" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=%CONFIG% -DCMAKE_PREFIX_PATH="%QTDIR%" %TESSERA_CMAKE_ARGS% || exit /b 1
"%CMAKE%" --build "%BUILD%" || exit /b 1
