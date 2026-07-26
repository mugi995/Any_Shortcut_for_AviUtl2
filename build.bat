@echo off
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found! Please compile using Developer Command Prompt for VS.
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
echo.
echo ========================================
echo  Building Any_Shortcut_for_AviUtl2.aux2 (x64)
echo ========================================
echo.

echo [1/2] Compiling resources...
rc.exe /nologo /c 65001 resource.rc
if %ERRORLEVEL% NEQ 0 (
    echo Resource compilation failed!
    exit /b 1
)

echo [2/2] Compiling and linking...
cl.exe /utf-8 /LD /MD /O2 /EHsc /W3 /wd4828 ^
    /D UNICODE /D _UNICODE ^
    /I "aviutl2_sdk" ^
    /Fe:"Any_Shortcut_for_AviUtl2.aux2" ^
    Every_shortcut.cpp ^
    CommandExecutor.cpp ^
    ConfigManager.cpp ^
    SettingDialog.cpp ^
    resource.res ^
    user32.lib shell32.lib comctl32.lib comdlg32.lib gdi32.lib

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build FAILED!
    exit /b 1
)

echo.
echo ========================================
echo  Build SUCCESS: Any_Shortcut_for_AviUtl2.aux2
echo ========================================
echo.

:: Clean up intermediate files
if exist resource.res del resource.res
if exist *.obj del *.obj

endlocal
