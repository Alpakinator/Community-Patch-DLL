@echo off
rem build_premise.cmd <outdir> - builds premise_fence_x64.exe and premise_fence_x86.exe (issue 3302, premise 1).
setlocal
set "SRC=%~dp0premise_fence.cpp"
set "OUT=%~1"
if "%OUT%"=="" (echo usage: build_premise.cmd ^<outdir^> & exit /b 2)
if not exist "%OUT%" mkdir "%OUT%"
set VSCMD_SKIP_SENDTELEMETRY=1
if "%VSDIR%"=="" set "VSDIR=C:\Program Files\Microsoft Visual Studio\2022\Community"
setlocal
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul || exit /b 1
cl /nologo /O2 /W3 /EHsc /DUNICODE /D_UNICODE "%SRC%" /Fo"%OUT%\p64.obj" /Fe"%OUT%\premise_fence_x64.exe" || exit /b 1
endlocal
setlocal
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cl /nologo /O2 /W3 /EHsc /DUNICODE /D_UNICODE "%SRC%" /Fo"%OUT%\p86.obj" /Fe"%OUT%\premise_fence_x86.exe" || exit /b 1
endlocal
echo BUILD OK
exit /b 0
