@echo off
rem shell\frame\build.cmd <outdir> [asan] - builds the ring hand-over (issue 3302) and its stand-ins:
rem   <outdir>\bin\ring_producer.exe   (x64) ring_sink.cpp + standin\ring_producer.cpp
rem   <outdir>\bin\ring_consumer.exe   (x86) ring_source.cpp + standin\ring_consumer.cpp
rem   <outdir>\bin\premise_fence_x64.exe / premise_fence_x86.exe (tests\premise: premise 1)
rem and compile-checks ring_sink.cpp on x86 and ring_source.cpp on x64 (both stay bitness-clean), C++14 like the server.
rem asan: the same with MSVC /fsanitize=address (/MD; the runtime DLLs copied beside the binaries). Test variant only.
rem Needs VS 2022 (VSDIR overridable) and the Windows SDK; no downloads. Nothing built is committed (rule 15: use D:).
setlocal
set "ROOT=%~dp0"
set "OUT=%~1"
set "VARIANT=%~2"
if "%OUT%"=="" (echo usage: build.cmd ^<outdir^> [asan] & exit /b 2)
set "CRT=/MT"
set "SAN="
if /i "%VARIANT%"=="asan" (set "CRT=/MD" & set "SAN=/fsanitize=address")
if not "%VARIANT%"=="" if /i not "%VARIANT%"=="asan" (echo unknown variant "%VARIANT%" & exit /b 2)
set VSCMD_SKIP_SENDTELEMETRY=1
if "%VSDIR%"=="" set "VSDIR=C:\Program Files\Microsoft Visual Studio\2022\Community"
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
set "BIN=%OUT%\bin"
for %%d in ("%BIN%" "%OUT%\obj64" "%OUT%\obj86" "%OUT%\chk") do if not exist "%%~d" mkdir "%%~d"
set "FLAGS=/nologo /O2 /Zi %CRT% %SAN% /W4 /EHsc /std:c++14 /DUNICODE /D_UNICODE /DNOMINMAX /I "%ROOT%..\common" /I "%ROOT%.""
echo output: %OUT% %SAN%

echo [1/3] x64: ring_producer.exe, premise_fence_x64.exe, ring_source.cpp compile check
setlocal
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul || exit /b 1
cl %FLAGS% "%ROOT%ring_sink.cpp" "%ROOT%standin\ring_producer.cpp" /Fo"%OUT%\obj64\\" /Fd"%OUT%\obj64\\" /Fe"%BIN%\ring_producer.exe" /link /DEBUG || exit /b 1
cl %FLAGS% /c "%ROOT%ring_source.cpp" /Fo"%OUT%\chk\ring_source64.obj" /Fd"%OUT%\chk\\" || exit /b 1
cl /nologo /O2 /Zi %CRT% %SAN% /W3 /EHsc "%ROOT%tests\premise\premise_fence.cpp" /Fo"%OUT%\obj64\premise.obj" /Fd"%OUT%\obj64\\" /Fe"%BIN%\premise_fence_x64.exe" /link /DEBUG || exit /b 1
endlocal

echo [2/3] x86: ring_consumer.exe, premise_fence_x86.exe, ring_sink.cpp compile check
setlocal
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cl %FLAGS% "%ROOT%ring_source.cpp" "%ROOT%standin\ring_consumer.cpp" /Fo"%OUT%\obj86\\" /Fd"%OUT%\obj86\\" /Fe"%BIN%\ring_consumer.exe" /link /DEBUG || exit /b 1
cl %FLAGS% /c "%ROOT%ring_sink.cpp" /Fo"%OUT%\chk\ring_sink86.obj" /Fd"%OUT%\chk\\" || exit /b 1
cl /nologo /O2 /Zi %CRT% %SAN% /W3 /EHsc "%ROOT%tests\premise\premise_fence.cpp" /Fo"%OUT%\obj86\premise.obj" /Fd"%OUT%\obj86\\" /Fe"%BIN%\premise_fence_x86.exe" /link /DEBUG || exit /b 1
endlocal

echo [3/3] runtimes
if defined SAN call :asanrt || exit /b 1
echo BUILD OK: %BIN%
exit /b 0

:asanrt
setlocal
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul || exit /b 1
copy /y "%VCToolsInstallDir%bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" "%BIN%\" >nul || (echo ASan runtime x64 missing & exit /b 1)
copy /y "%VCToolsInstallDir%bin\Hostx64\x86\clang_rt.asan_dynamic-i386.dll" "%BIN%\" >nul || (echo ASan runtime x86 missing & exit /b 1)
echo ASan runtimes copied from %VCToolsInstallDir%
endlocal & exit /b 0
