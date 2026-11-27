@echo off
rem 0805: builds modlist.exe (x86) into %1 (default F:\c5-64bit-work\0805\bin).
setlocal
set VSCMD_SKIP_SENDTELEMETRY=1
set OUT=%~1
if "%OUT%"=="" set OUT=F:\c5-64bit-work\0805\bin
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
"C:\Program Files\LLVM\bin\clang-cl.exe" --target=i686-pc-windows-msvc /nologo /O2 /MT "%~dp0modlist.cpp" /Fe:"%OUT%\modlist.exe" /Fo:"%OUT%\modlist.obj" /link psapi.lib || exit /b 1
echo built %OUT%\modlist.exe
