@echo off
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found! Please compile using Developer Command Prompt.
    goto :build
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
  set InstallDir=%%i
)

if "%InstallDir%"=="" (
    echo Visual Studio installation with VC++ not found.
    goto :build
)

if exist "%InstallDir%\VC\Auxiliary\Build\vcvars64.bat" (
    call "%InstallDir%\VC\Auxiliary\Build\vcvars64.bat"
) else (
    echo vcvars64.bat not found.
)

:build
cd /d "%~dp0"
echo Compiling resources...
rc.exe /nologo resource.rc
if %ERRORLEVEL% NEQ 0 (
    echo Resource compilation failed!
    exit /b 1
)

echo Compiling SrtExportAuo.cpp and Generator.cpp as 64-bit DLL (Output Plugin)...
cl.exe /utf-8 /LD /MD /O2 /EHsc /W3 /Fe:SrtExport.auo2 SrtExportAuo.cpp Generator.cpp resource.res user32.lib shell32.lib
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b 1
)
echo Build succeeded!
