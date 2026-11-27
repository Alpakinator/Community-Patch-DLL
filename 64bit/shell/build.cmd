@echo off
rem Origin (rule 13, decision D22): the project's fork of Community-Patch-DLL, branch claude/shell-gates-d8e422,
rem path experiments/shell-gates/compositor/build.cmd, copied at branch head c60138b82 (file last changed in a9d8328e2).
rem Copied into shell/build.cmd on 2026-11-10 (issue 3001) and adapted to the shell/ layout; later changes are in
rem this repository's history.
rem
rem shell\build.cmd - builds the shell-side programs from a clean shell (run from anywhere):
rem   bin\shell_server.exe    (x64)  test server: renders the "world" into a shared texture
rem   bin\shell_composite.dll (x86)  composite DLL loaded into the EXE (or into test_host.exe)
rem   bin\test_host.exe       (x86)  stand-in for CivilizationV_DX11.exe
rem   bin\shell_composite.ini        default config (copied once, never overwritten)
rem Output folder (rule 15, decision D37): %C5_SHELL_BUILD%, required since 2026-11-11 (see below); formerly else D:\c5-64bit-work\3000\shell-build when
rem D:\c5-64bit-work exists, else shell\build (git-ignored). Nothing built is committed. Since D133 (2026-11-14) build
rem trees go on C:, e.g. C:\c5-64bit-work\<epic>\<you>\shell-build, never on D: (ticket 0193).
rem Needs VS 2022 (MSVC v143, VSDIR overridable) and Windows SDK 10.0.26100.0 (fxc, SDKVER overridable). No downloads.
rem
rem shell\build.cmd asan (issue 3013): the same three programs and MinHook with MSVC /fsanitize=address, a TEST VARIANT
rem ONLY (never loaded into the game). Dynamic CRT (/MD: MSVC's ASan runtime is one DLL per process, so the host and the
rem composite DLL it loads are both instrumented and share it), no /SAFESEH (the ASan libraries carry no SafeSEH
rem table), and the runtime DLLs clang_rt.asan_dynamic-i386.dll / -x86_64.dll copied from the MSVC toolset into bin\
rem beside the binaries (never into a game install). Output folder: %C5_SHELL_BUILD%, required as for the plain build
rem (issue 3018 V8, 2026-11-11: no shared default; the old default D:\c5-64bit-work\3000\shell-build-asan is gone).
rem
rem Epic 3200 (decision D58 point 4): shell_server.exe links render\'s static library c5render (the terrain draw,
rem c5r::terrain) and therefore builds with the dynamic CRT /MD like render\ (clang-cl /MD, C++14, D15); the composite
rem DLL and test_host.exe stay /MT. c5render is built first by render\build.py into %OUT%\render (its launch gate
rem applies: exit 3 while another team holds the quiet-machine lock). The asan variant links the same non-instrumented
rem c5render and turns off the STL's container annotations on the server side (_DISABLE_*_ANNOTATION), which would
rem otherwise refuse to link instrumented and non-instrumented objects together.
rem Several checkouts building at once: set C5_SHELL_BUILD per checkout (the default folder is shared).
setlocal
rem bug 0108: vcvarsall otherwise starts a background PowerShell for its telemetry
set VSCMD_SKIP_SENDTELEMETRY=1
set "ROOT=%~dp0"
set "VARIANT=%~1"
set "CRT=/MT"
set "CRT64=/MD"
set "SAN="
set "SAN64DEF="
set "SAFESEH=/SAFESEH"
set "SUFFIX="
if /i "%VARIANT%"=="asan" (set "CRT=/MD" & set "SAN=/fsanitize=address" & set "SAN64DEF=/D_DISABLE_VECTOR_ANNOTATION /D_DISABLE_STRING_ANNOTATION" & set "SAFESEH=" & set "SUFFIX=-asan")
if not "%VARIANT%"=="" if /i not "%VARIANT%"=="asan" (echo unknown variant "%VARIANT%": no argument or asan & exit /b 2)
if "%VSDIR%"=="" set "VSDIR=C:\Program Files\Microsoft Visual Studio\2022\Community"
if "%SDKVER%"=="" set "SDKVER=10.0.26100.0"
set "FXC=C:\Program Files (x86)\Windows Kits\10\bin\%SDKVER%\x64\fxc.exe"
rem MC order 2026-11-11 14:08 (incident: a worker's build overwrote the shared default folder): no shared default any
rem more. C5_SHELL_BUILD must name this checkout's own output folder; without it the build refuses with exit 2.
set "OUT=%C5_SHELL_BUILD%"
if "%OUT%"=="" (echo shell\build.cmd: C5_SHELL_BUILD is not set: set it to this checkout's own output folder, e.g. C:\c5-64bit-work\^<epic^>\^<you^>\shell-build%SUFFIX% - refused, nothing built & exit /b 2)
set "GEN=%OUT%\gen"
set "BIN=%OUT%\bin"
set "OBJ64=%OUT%\obj64"
set "OBJ86=%OUT%\obj86"
set "MHOBJ=%OUT%\minhook86"
set "RBUILD=%OUT%\render"
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
for %%d in ("%GEN%" "%BIN%" "%OBJ64%" "%OBJ86%" "%MHOBJ%") do if not exist "%%~d" mkdir "%%~d"
echo output: %OUT% %SAN%

rem Ticket 4960 (epic 0590, D202): the EXE address header vp_exe_tables.gen.h from the local per-build tables (setting
rem exe_tables, C5_EXE_TABLES) into %GEN%; every program below has exebuild\include and %GEN% on its include path.
rem Without the tables the build fails and the emitter names the setting.
echo [0a/5] EXE address header (tools\exebuild\emit_header.py)
python -I "%ROOT%..\tools\exebuild\emit_header.py" --out "%GEN%"
if errorlevel 1 goto :fail
set EXEINC=/I "%ROOT%..\exebuild\include" /I "%GEN%"

echo [0/5] c5render (x64 static library, render\build.py --target c5render)
python "%ROOT%..\render\build.py" --build-dir "%RBUILD%" --target c5render
if errorlevel 3 if not errorlevel 4 (echo render\build.py: refused by the launch gate - exit 3 & exit /b 3)
if errorlevel 1 goto :fail
if not exist "%RBUILD%\c5render.lib" (echo c5render.lib missing in %RBUILD% & goto :fail)

echo [1/5] shaders (fxc, shader model 4.0)
call :fxc vs_4_0 VSFullscreen || goto :fail
call :fxc ps_4_0 PSComposite || goto :fail
call :fxc ps_4_0 PSCompositeDepth || goto :fail
call :fxc ps_4_0 PSServer || goto :fail
call :fxc vs_4_0 VSHostUi || goto :fail
call :fxc ps_4_0 PSHostUi || goto :fail
call :fxc vs_4_0 VSGrid || goto :fail
call :fxc ps_4_0 PSGrid || goto :fail
rem ticket 0407: the fow-models resample pass (composite\fow_models_resample.hlsl), built in so that the composite never
rem loads d3dcompiler_47 nor compiles on the EXE's render thread; /O1 is D3DCompile's default level, which 0394 measured
call :fxcf vs_4_0 VS FowRsVS "%ROOT%composite\fow_models_resample.hlsl" || goto :fail
call :fxcf ps_4_0 PS FowRsPS "%ROOT%composite\fow_models_resample.hlsl" || goto :fail

echo [2/5] shell_server.exe (x64)
call :build64 || goto :fail
echo [3/5] MinHook (x86, static, from source in third_party\minhook)
call :minhook86 || goto :fail
echo [4/5] shell_composite.dll + test_host.exe (x86)
call :build86 || goto :fail
echo [5/5] default config
if not exist "%BIN%\shell_composite.ini" copy /y "%ROOT%composite\shell_composite.ini" "%BIN%\shell_composite.ini" >nul
if defined SAN call :asanrt || goto :fail
echo BUILD OK: %BIN%
exit /b 0

:fail
echo BUILD FAILED
exit /b 1

:fxc
"%FXC%" /nologo /O3 /T %1 /E %2 /Fh "%GEN%\%2.h" /Vn g_%2 "%ROOT%common\shaders.hlsl" >nul || exit /b 1
exit /b 0

:fxcf
rem profile, entry point, output name (gen\<name>.h, array g_<name>), source file
"%FXC%" /nologo /O1 /T %1 /E %2 /Fh "%GEN%\%3.h" /Vn g_%3 %4 >nul || exit /b 1
exit /b 0

:build64
setlocal
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul || exit /b 1
rem issue 3407: the options loader (options\src, compiled in as the server does); the residency and the bakers are in c5render
cl /nologo /O2 %CRT64% %SAN% %SAN64DEF% /W3 /EHsc /Zi %EXEINC% /DUNICODE /D_UNICODE /DNOMINMAX /I "%GEN%" /I "%ROOT%common" /I "%ROOT%..\render\include" ^
   /I "%ROOT%..\options\include" ^
   "%ROOT%server\shell_server.cpp" "%ROOT%..\options\src\vp64_options.cpp" ^
   /Fo"%OBJ64%\\" /Fd"%OBJ64%\\" /Fe"%BIN%\shell_server.exe" ^
   /link /DEBUG /OPT:REF "%RBUILD%\c5render.lib" d3d11.lib dxgi.lib dxguid.lib d3dcompiler.lib windowscodecs.lib ole32.lib || exit /b 1
rem issue 3304: lockstep_server.exe, the stand-in version-3 server of shell\tests\test_lockstep.py (test program only)
cl /nologo /O2 %CRT64% %SAN% %SAN64DEF% /W3 /EHsc /Zi %EXEINC% /DUNICODE /D_UNICODE /DNOMINMAX /I "%ROOT%common" ^
   "%ROOT%tests\lockstep_server.cpp" /Fo"%OBJ64%\\" /Fd"%OBJ64%\lockstep_server.pdb" /Fe"%BIN%\lockstep_server.exe" ^
   /link /DEBUG d3d11.lib dxgi.lib || exit /b 1
endlocal & exit /b 0

:minhook86
if not exist "%ROOT%third_party\minhook\src\hook.c" (echo MinHook source missing in shell\third_party\minhook& exit /b 1)
setlocal
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
set "MH=%ROOT%third_party\minhook"
cl /nologo /c /O2 %CRT% %SAN% /W3 /Zi /I "%MH%\include" "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" ^
   "%MH%\src\hde\hde32.c" /Fo"%MHOBJ%\\" /Fd"%MHOBJ%\minhook.pdb" || exit /b 1
lib /nologo /OUT:"%MHOBJ%\minhook.x86.lib" "%MHOBJ%\buffer.obj" "%MHOBJ%\hook.obj" "%MHOBJ%\trampoline.obj" "%MHOBJ%\hde32.obj" || exit /b 1
endlocal & exit /b 0

:build86
setlocal
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
set "MHLIB=%MHOBJ%\minhook.x86.lib"
set MHDEF=/I "%ROOT%third_party\minhook\include"
rem /MT: static CRT, so the DLL brings no runtime dependency into the game process (asan variant: /MD, see the top)
rem issue 3304: the ring source of issue 3302 (shell\frame\ring_source.cpp, used with handover=ring) is linked in
rem ticket 0778: /PDBALTPATH:%%_PDB%% (as REPRO_LINK in tools\x64-dll\build.py) so the shipped DLL's debug directory names
rem only shell_composite.pdb, not the build folder's absolute path; %% doubled because this is a batch file
cl /nologo /O2 %CRT% %SAN% /W3 /EHsc /Zi %EXEINC% /DUNICODE /D_UNICODE /I "%GEN%" /I "%ROOT%common" /I "%ROOT%frame" %MHDEF% /LD ^
   "%ROOT%composite\shell_composite.cpp" "%ROOT%composite\bake_tap.cpp" "%ROOT%composite\water_tap.cpp" "%ROOT%composite\overlay_tap.cpp" "%ROOT%composite\decal_tap.cpp" "%ROOT%composite\shadow_tap.cpp" "%ROOT%composite\ice_tap.cpp" "%ROOT%composite\forest_tap.cpp" "%ROOT%composite\cut_tap.cpp" "%ROOT%composite\fow_models.cpp" "%ROOT%composite\unit_filter.cpp" "%ROOT%composite\farm_guard.cpp" "%ROOT%composite\vtable_index.c" "%ROOT%frame\ring_source.cpp" ^
   /Fo"%OBJ86%\\" /Fd"%OBJ86%\\" /Fe"%BIN%\shell_composite.dll" /link /DEBUG /OPT:REF /PDBALTPATH:%%_PDB%% %SAFESEH% %MHLIB% || exit /b 1
cl /nologo /O2 %CRT% %SAN% /W3 /EHsc /Zi %EXEINC% /DUNICODE /D_UNICODE /I "%GEN%" /I "%ROOT%common" "%ROOT%standin\test_host.cpp" ^
   /Fo"%OBJ86%\\" /Fd"%OBJ86%\\" /Fe"%BIN%\test_host.exe" /link /DEBUG /OPT:REF /SUBSYSTEM:WINDOWS user32.lib shell32.lib || exit /b 1
rem issue 4102: decal_tap_host.exe, the test entry of composite\decal_tap.cpp (shell\tests\test_decal_tap.py; test program only)
cl /nologo /O2 %CRT% %SAN% /W3 /EHsc /Zi %EXEINC% /DUNICODE /D_UNICODE /I "%ROOT%common" /I "%ROOT%composite" "%ROOT%tests\decal_tap_host.cpp" ^
   "%ROOT%composite\decal_tap.cpp" /Fo"%OBJ86%\\" /Fd"%OBJ86%\decal_tap_host.pdb" /Fe"%BIN%\decal_tap_host.exe" /link /DEBUG || exit /b 1
rem ticket 0420: forest_tap_host.exe, the test entry of composite\forest_tap.cpp (shell\tests\test_forest_tap.py; test program only)
cl /nologo /O2 %CRT% %SAN% /W3 /EHsc /Zi %EXEINC% /DUNICODE /D_UNICODE /I "%ROOT%common" /I "%ROOT%composite" "%ROOT%tests\forest_tap_host.cpp" ^
   "%ROOT%composite\forest_tap.cpp" /Fo"%OBJ86%\\" /Fd"%OBJ86%\forest_tap_host.pdb" /Fe"%BIN%\forest_tap_host.exe" /link /DEBUG || exit /b 1
rem ticket 4553: cut_tap_host.exe, the test entry of composite\cut_tap.cpp (shell\tests\test_cut_tap.py; test program only)
cl /nologo /O2 %CRT% %SAN% /W3 /EHsc /Zi %EXEINC% /DUNICODE /D_UNICODE /I "%ROOT%common" /I "%ROOT%composite" "%ROOT%tests\cut_tap_host.cpp" ^
   "%ROOT%composite\cut_tap.cpp" /Fo"%OBJ86%\\" /Fd"%OBJ86%\cut_tap_host.pdb" /Fe"%BIN%\cut_tap_host.exe" /link /DEBUG || exit /b 1
rem issue 4752: unit_filter_host.exe, the stand-in of the unit-draw filter (shell\tests\test_unit_filter.py; test program only:
rem it compiles its test shaders with d3dcompiler_47 at run time and loads shell_composite.dll like test_host.exe)
cl /nologo /O2 %CRT% %SAN% /W3 /EHsc /Zi %EXEINC% /DUNICODE /D_UNICODE /I "%ROOT%common" "%ROOT%tests\unit_filter_host.cpp" ^
   /Fo"%OBJ86%\\" /Fd"%OBJ86%\unit_filter_host.pdb" /Fe"%BIN%\unit_filter_host.exe" /link /DEBUG /SUBSYSTEM:WINDOWS user32.lib shell32.lib d3d11.lib dxgi.lib d3dcompiler.lib || exit /b 1
rem issue 0150: lk_late_test.exe, the unit test of composite\lk_late.h (shell\tests\test_lk_late.py; test program only)
cl /nologo /O2 %CRT% %SAN% /W3 /EHsc /Zi %EXEINC% /I "%ROOT%composite" "%ROOT%tests\lk_late_test.cpp" ^
   /Fo"%OBJ86%\\" /Fd"%OBJ86%\lk_late_test.pdb" /Fe"%BIN%\lk_late_test.exe" /link /DEBUG || exit /b 1
rem ticket 0527: farm_guard_test.exe, the unit test of composite\farm_guard.cpp (shell\tests\test_farm_guard.py; test program only)
cl /nologo /O2 %CRT% %SAN% /W3 /EHsc /Zi %EXEINC% /DUNICODE /D_UNICODE /I "%ROOT%common" /I "%ROOT%composite" "%ROOT%tests\farm_guard_test.cpp" ^
   "%ROOT%composite\farm_guard.cpp" /Fo"%OBJ86%\\" /Fd"%OBJ86%\farm_guard_test.pdb" /Fe"%BIN%\farm_guard_test.exe" /link /DEBUG || exit /b 1
rem ticket 0535: farm_guard_peer.dll, the same test file as a second module with its own copy of composite\farm_guard.cpp (the
rem composite beside the proxy's guard in one process; loaded by farm_guard_test.exe; test program only)
if not exist "%OBJ86%\peer" mkdir "%OBJ86%\peer"
cl /nologo /O2 %CRT% %SAN% /W3 /EHsc /Zi %EXEINC% /DUNICODE /D_UNICODE /DFARM_GUARD_PEER_DLL /LD /I "%ROOT%common" /I "%ROOT%composite" "%ROOT%tests\farm_guard_test.cpp" ^
   "%ROOT%composite\farm_guard.cpp" /Fo"%OBJ86%\peer\\" /Fd"%OBJ86%\peer\farm_guard_peer.pdb" /Fe"%BIN%\farm_guard_peer.dll" /link /DEBUG || exit /b 1
endlocal & exit /b 0

:asanrt
rem the ASan runtimes of the toolset vcvarsall selects (VCToolsInstallDir), copied beside the binaries
setlocal
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul || exit /b 1
copy /y "%VCToolsInstallDir%bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" "%BIN%\" >nul || (echo ASan runtime x64 missing in %VCToolsInstallDir% & exit /b 1)
copy /y "%VCToolsInstallDir%bin\Hostx64\x86\clang_rt.asan_dynamic-i386.dll" "%BIN%\" >nul || (echo ASan runtime x86 missing in %VCToolsInstallDir% & exit /b 1)
echo ASan runtimes copied from %VCToolsInstallDir%
endlocal & exit /b 0
