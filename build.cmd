@echo off
rem Builds the Canon LBP-810 driver package (amd64 + x86) into out\package and the MSI custom
rem actions into out\setup (scripts\make-msi.py packages them; release.cmd runs everything).
rem Usage: build.cmd [amd64|x86]   (default: both)
setlocal
set "ROOT=%~dp0"
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
set "VSDIR="
for /f "usebackq tokens=*" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio with C++ tools not found
    exit /b 1
)
set "ARCHS=%~1"
if "%ARCHS%"=="" set "ARCHS=amd64 x86"
for %%a in (%ARCHS%) do (
    call :build %%a || exit /b 1
)
echo Build OK
exit /b 0

:build
setlocal
set "ARCH=%~1"
if "%ARCH%"=="amd64" (set "VCARCH=x64") else (set "VCARCH=x86")
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %VCARCH% >nul || exit /b 1
set "OBJ=%ROOT%build\%ARCH%"
set "OUT=%ROOT%out\package\%ARCH%"
set "TOOLS=%ROOT%out\tools\%ARCH%"
set "SETUP=%ROOT%out\setup\%ARCH%"
if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%TOOLS%" mkdir "%TOOLS%"
if not exist "%SETUP%" mkdir "%SETUP%"
set "SRC=%ROOT%src"
set CFLAGS=/nologo /W4 /WX- /O2 /MT /EHsc /GS /Zi /DUNICODE /D_UNICODE /DNOMINMAX /std:c++17 /utf-8
set LFLAGS=/nologo /DLL /DEBUG /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT

echo === %ARCH%: CNLB810R.DLL (rendering plug-in)
cl %CFLAGS% /c /Fo"%OBJ%\\" /Fd"%OBJ%\render.pdb" "%SRC%\driver\render\render.cpp" "%SRC%\common\spool.cpp" "%SRC%\common\scoa.cpp" || exit /b 1
rc /nologo /fo "%OBJ%\render.res" "%SRC%\driver\render\render.rc" || exit /b 1
link %LFLAGS% /DEF:"%SRC%\driver\render\render.def" /OUT:"%OUT%\CNLB810R.DLL" /IMPLIB:"%OBJ%\CNLB810R.lib" /PDB:"%OBJ%\CNLB810R.pdb" "%OBJ%\render.obj" "%OBJ%\spool.obj" "%OBJ%\scoa.obj" "%OBJ%\render.res" winspool.lib advapi32.lib user32.lib gdi32.lib uuid.lib || exit /b 1

echo === %ARCH%: CNLB810L.DLL (language monitor)
cl %CFLAGS% /c /Fo"%OBJ%\\" /Fd"%OBJ%\langmon.pdb" "%SRC%\driver\langmon\langmon.cpp" "%SRC%\common\engine.cpp" || exit /b 1
rc /nologo /fo "%OBJ%\langmon.res" "%SRC%\driver\langmon\langmon.rc" || exit /b 1
link %LFLAGS% /DEF:"%SRC%\driver\langmon\langmon.def" /OUT:"%OUT%\CNLB810L.DLL" /IMPLIB:"%OBJ%\CNLB810L.lib" /PDB:"%OBJ%\CNLB810L.pdb" "%OBJ%\langmon.obj" "%OBJ%\engine.obj" "%OBJ%\spool.obj" "%OBJ%\scoa.obj" "%OBJ%\langmon.res" winspool.lib advapi32.lib user32.lib || exit /b 1

echo === %ARCH%: CNLB810U.DLL (user interface plug-in)
rem Canon's icons and bitmaps come from the original driver at build time (placeholders without it):
rem the folder with CAP1UIK.DLL, %CANON_DRIVER% or LBP-810_R110_V110_Win_x32_RU_7\WinVista.
set "CANON=%CANON_DRIVER%"
if not defined CANON set "CANON=%ROOT%LBP-810_R110_V110_Win_x32_RU_7\WinVista"
if not exist "%CANON%\CAP1UIK.DLL" set "CANON="
python "%ROOT%scripts\canon-assets.py" "%CANON%" "%ROOT%build\assets" || exit /b 1
cl %CFLAGS% /c /Fo"%OBJ%\\" /Fd"%OBJ%\ui.pdb" "%SRC%\driver\ui\ui.cpp" "%SRC%\driver\ui\pages.cpp" "%SRC%\driver\ui\preview.cpp" "%SRC%\driver\ui\htpreview.cpp" "%SRC%\driver\ui\profiles.cpp" "%SRC%\driver\ui\dialogs.cpp" || exit /b 1
rc /nologo /i "%ROOT%build\assets" /fo "%OBJ%\ui.res" "%SRC%\driver\ui\ui.rc" || exit /b 1
link %LFLAGS% /DEF:"%SRC%\driver\ui\ui.def" /OUT:"%OUT%\CNLB810U.DLL" /IMPLIB:"%OBJ%\CNLB810U.lib" /PDB:"%OBJ%\CNLB810U.pdb" "%OBJ%\ui.obj" "%OBJ%\pages.obj" "%OBJ%\preview.obj" "%OBJ%\htpreview.obj" "%OBJ%\profiles.obj" "%OBJ%\dialogs.obj" "%OBJ%\ui.res" winspool.lib advapi32.lib user32.lib gdi32.lib comctl32.lib comdlg32.lib version.lib uuid.lib ole32.lib || exit /b 1

echo === %ARCH%: LB810RES.DLL (resources)
rc /nologo /fo "%OBJ%\lb810res.res" "%SRC%\driver\res\lb810res.rc" || exit /b 1
link /nologo /DLL /NOENTRY /MACHINE:%VCARCH% /OUT:"%OUT%\LB810RES.DLL" "%OBJ%\lb810res.res" || exit /b 1

echo === %ARCH%: data files
copy /y "%SRC%\driver\gpd\CNLB810.GPD" "%OUT%\" >nul || exit /b 1
copy /y "%SRC%\driver\gpd\CNLB810.INI" "%OUT%\" >nul || exit /b 1

echo === %ARCH%: LBP810CA.DLL (MSI custom actions)
cl %CFLAGS% /c /Fo"%OBJ%\\" /Fd"%OBJ%\setupca.pdb" "%SRC%\setup\setupca.cpp" || exit /b 1
rc /nologo /fo "%OBJ%\setupca.res" "%SRC%\setup\setupca.rc" || exit /b 1
link %LFLAGS% /DEF:"%SRC%\setup\setupca.def" /OUT:"%SETUP%\LBP810CA.DLL" /IMPLIB:"%OBJ%\LBP810CA.lib" /PDB:"%OBJ%\LBP810CA.pdb" "%OBJ%\setupca.obj" "%OBJ%\setupca.res" msi.lib setupapi.lib newdev.lib cfgmgr32.lib winspool.lib crypt32.lib advapi32.lib || exit /b 1

echo === %ARCH%: captcli.exe (diagnostics)
cl %CFLAGS% /Fo"%OBJ%\\" /Fd"%OBJ%\captcli.pdb" /Fe"%TOOLS%\captcli.exe" "%SRC%\tools\captcli.cpp" "%SRC%\tools\usbdev.cpp" "%SRC%\common\engine.cpp" "%SRC%\common\scoa.cpp" "%SRC%\common\spool.cpp" /link /DEBUG gdi32.lib user32.lib setupapi.lib || exit /b 1
cl %CFLAGS% /Fo"%OBJ%\\" /Fd"%OBJ%\lmtest.pdb" /Fe"%TOOLS%\lmtest.exe" "%SRC%\tools\dev\lmtest.cpp" "%SRC%\tools\usbdev.cpp" /link /DEBUG setupapi.lib || exit /b 1
exit /b 0
