@echo off
setlocal enabledelayedexpansion
set IMAGE=vp-dll-builder
set CONFIG=release
set DO_43_CIVS=

:parse
if "%1"=="" goto :build
if /i "%1"=="--config" (
    set CONFIG=%2
    shift
    shift
    goto :parse
)
if /i "%1"=="--43-civs" (
    set DO_43_CIVS=--43-civs
    shift
    goto :parse
)
if /i "%1"=="--help" goto :help
if /i "%1"=="-h" goto :help
echo Unknown option: %1
exit /b 1

:help
echo Usage: build.bat [--config release^|debug] [--43-civs]
echo.
echo   --config release^|debug   Build configuration (default: release)
echo   --43-civs                Build 43-civ version
exit /b 0

:build
docker image inspect %IMAGE% >nul 2>&1
if errorlevel 1 (
    echo Building Docker image (first time only, ~15 min^)...
    docker build -t %IMAGE% .
    if errorlevel 1 goto :fail
)

if "%DO_43_CIVS%"=="--43-civs" (
    echo Building VP DLL - %CONFIG% 43-civs
) else (
    echo Building VP DLL - %CONFIG%
)
docker run --rm -v "%cd%:/workspace" %IMAGE% --config %CONFIG% %DO_43_CIVS%
if errorlevel 1 goto :fail

echo.
echo DLL: clang-output\%CONFIG:~0,1%%CONFIG:~1%\CvGameCore_Expansion2.dll
goto :end

:fail
echo.
echo BUILD FAILED. Is Docker Desktop running?
:end
pause
