@echo off
rem Creates a self-contained folder dist\Tessera with the executable, the Qt runtime and the
rem Visual C++ runtime DLLs (app-local, no installer needed). Run build.cmd first.
rem Optional: deploy.cmd zip  -> also creates dist\Tessera-<version>-win64.zip
rem           deploy.cmd installer -> also builds dist\Tessera-<version>-setup.exe (needs Inno Setup 6)
setlocal
if not defined QTDIR set QTDIR=C:\Qt\6.10.3\msvc2022_64
set VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

set DIST=%~dp0dist\Tessera
if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%" || exit /b 1
copy /y "%~dp0build\Release\Tessera.exe" "%DIST%\" >nul || exit /b 1
"%QTDIR%\bin\windeployqt.exe" --release --no-quick-import --no-system-d3d-compiler --no-opengl-sw ^
    --no-compiler-runtime "%DIST%\Tessera.exe" || exit /b 1

rem VCToolsRedistDir is set by vcvars64.bat
copy /y "%VCToolsRedistDir%x64\Microsoft.VC143.CRT\*.dll" "%DIST%\" >nul || exit /b 1
echo Pronto: %DIST%

if /i "%1"=="zip" (
    for /f "tokens=3" %%v in ('findstr /r /c:"project(Tessera VERSION" "%~dp0CMakeLists.txt"') do set VERSION=%%v
    call set ZIP=%~dp0dist\Tessera-%%VERSION%%-win64.zip
    call powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\makezip.ps1" "%DIST%" "%%ZIP%%" || exit /b 1
    call echo Archivio: %%ZIP%%
)

if /i "%1"=="installer" (
    for /f "tokens=3" %%v in ('findstr /r /c:"project(Tessera VERSION" "%~dp0CMakeLists.txt"') do set VERSION=%%v
    set ISCC=
    for %%p in ("%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe") do if exist %%p set ISCC=%%~p
    if not defined ISCC (
        echo Inno Setup 6 non trovato: installarlo da https://jrsoftware.org/isinfo.php e rilanciare.
        exit /b 1
    )
    call "%%ISCC%%" /DAppVersion=%%VERSION%% "%~dp0installer\tessera.iss" || exit /b 1
)
