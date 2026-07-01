@echo off
setlocal enabledelayedexpansion
set IMAGE=vp-dll-builder
set CONFIG=release
set DO_43_CIVS=
set DO_BUILD_IMAGE=

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
if /i "%1"=="--build" (
    set DO_BUILD_IMAGE=1
    shift
    goto :parse
)
if /i "%1"=="--help" goto :help
if /i "%1"=="-h" goto :help
echo Unknown option: %1
exit /b 1

:help
echo Usage: docker-build.bat [--config release^|debug] [--build] [--43-civs]
echo.
echo   --config release^|debug   Build configuration (default: release)
echo   --build                  Rebuild Docker image first
echo   --43-civs                Build 43-civ version
exit /b 0

:build
if defined DO_BUILD_IMAGE goto :build_image
docker image inspect %IMAGE% >nul 2>&1
if errorlevel 1 goto :build_image
goto :run

:build_image
echo Building Docker image (first time only, ~15 min^)...
docker build -t %IMAGE% .
if errorlevel 1 goto :fail

:run

if "%DO_43_CIVS%"=="--43-civs" (
    echo Building VP DLL - %CONFIG% 43-civs
) else (
    echo Building VP DLL - %CONFIG%
)
docker run --rm -e PYTHONUNBUFFERED=1 -v "%cd%:/workspace" %IMAGE% --config %CONFIG% %DO_43_CIVS%
if errorlevel 1 goto :fail

echo.
echo DLL: clang-output\%CONFIG:~0,1%%CONFIG:~1%\CvGameCore_Expansion2.dll
goto :end

:fail
echo.
echo BUILD FAILED. Is Docker Desktop running?
:end
if not "%VP_NO_PAUSE%"=="1" pause
exit /b %errorlevel%
