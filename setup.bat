@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem CommonwealthMP root bootstrap:
rem   1) git submodules (commonlibf4)
rem   2) Scaleform Flex toolchain under interface\swf\_tools (optional rebuild of Menu SWF)
rem
rem Usage:
rem   setup.bat              submodules + Flex tools if missing
rem   setup.bat --deps-only  submodules only
rem   setup.bat --flex-only  Flex tools only
rem   setup.bat --force-flex re-download / re-extract Flex tools

set "ROOT=%~dp0"
cd /d "%ROOT%" || exit /b 1

set "DO_DEPS=1"
set "DO_FLEX=1"
set "FORCE_FLEX=0"

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--deps-only" (
  set "DO_FLEX=0"
  shift & goto parse
)
if /I "%~1"=="--flex-only" (
  set "DO_DEPS=0"
  shift & goto parse
)
if /I "%~1"=="--force-flex" (
  set "FORCE_FLEX=1"
  shift & goto parse
)
if /I "%~1"=="-h" goto usage
if /I "%~1"=="--help" goto usage
echo Unknown option: %~1
goto usage

:parsed

if "%DO_DEPS%"=="1" (
  echo ==^> git submodules
  where git >nul 2>&1
  if errorlevel 1 (
    echo ERROR: git not found on PATH.
    exit /b 1
  )
  git submodule update --init --recursive
  if errorlevel 1 (
    echo ERROR: git submodule update failed.
    exit /b 1
  )
)

if "%DO_FLEX%"=="1" (
  call :setup_flex
  if errorlevel 1 exit /b 1
)

echo.
echo Setup complete.
echo   Plugin:  cd plugin ^&^& xmake f -m releasedbg -a x64 ^&^& xmake build
echo   Menu SWF: cd interface\swf ^&^& powershell -ExecutionPolicy Bypass -File .\build.ps1
echo   Pack:    powershell -ExecutionPolicy Bypass -File scripts\pack-mo2.ps1 -Build
exit /b 0

:usage
echo Usage: setup.bat [--deps-only ^| --flex-only ^| --force-flex]
exit /b 1

:setup_flex
set "TOOLS=%ROOT%interface\swf\_tools"
set "FLEX=%TOOLS%\adobe-flex"
set "PG_DIR=%TOOLS%\player\32.0"
set "PG=%PG_DIR%\playerglobal.swc"
set "ZIP=%TOOLS%\apache-flex-sdk-4.16.1-bin.zip"
set "FLEX_URL=https://archive.apache.org/dist/flex/4.16.1/binaries/apache-flex-sdk-4.16.1-bin.zip"
rem Adobe Flash Player 32 playerglobal (required by mxmlc -external-library-path)
set "PG_URL=https://fpdownload.macromedia.com/get/flashplayer/updaters/32/playerglobal32_0.swc"

if "%FORCE_FLEX%"=="1" (
  if exist "%FLEX%" (
    echo ==^> removing existing Flex SDK at "%FLEX%"
    rmdir /s /q "%FLEX%"
  )
)

if exist "%FLEX%\lib\mxmlc.jar" if exist "%PG%" if "%FORCE_FLEX%"=="0" (
  echo ==^> Flex toolchain already present
  echo     SDK: %FLEX%
  echo     playerglobal: %PG%
  exit /b 0
)

echo ==^> Flex toolchain -^> "%TOOLS%"
if not exist "%TOOLS%" mkdir "%TOOLS%"

where tar >nul 2>&1
if errorlevel 1 (
  echo ERROR: tar not found. Use Windows 10+ tar, or extract the Flex SDK zip manually to:
  echo   %FLEX%
  exit /b 1
)

if not exist "%FLEX%\lib\mxmlc.jar" (
  if not exist "%ZIP%" (
    echo     downloading Apache Flex SDK 4.16.1 ...
    curl -fL --retry 3 -o "%ZIP%" "%FLEX_URL%"
    if errorlevel 1 (
      echo ERROR: download failed: %FLEX_URL%
      echo Manual: place apache-flex-sdk-4.16.1-bin.zip in "%TOOLS%" and re-run.
      exit /b 1
    )
  )
  echo     extracting to "%FLEX%" ...
  if exist "%FLEX%" rmdir /s /q "%FLEX%"
  mkdir "%FLEX%"
  tar -xf "%ZIP%" -C "%FLEX%" --strip-components=1
  if errorlevel 1 (
    echo ERROR: extract failed. Delete "%ZIP%" and retry, or extract manually.
    exit /b 1
  )
  if not exist "%FLEX%\lib\mxmlc.jar" (
    echo ERROR: mxmlc.jar missing after extract. Unexpected zip layout.
    exit /b 1
  )
)

if not exist "%PG%" (
  echo     downloading playerglobal.swc (Flash Player 32) ...
  if not exist "%PG_DIR%" mkdir "%PG_DIR%"
  curl -fL --retry 3 -o "%PG%" "%PG_URL%"
  if errorlevel 1 (
    echo ERROR: playerglobal download failed: %PG_URL%
    echo Manual: save playerglobal.swc to "%PG%"
    exit /b 1
  )
)

echo     Flex SDK ready: %FLEX%
echo     playerglobal: %PG%
echo     Optional: set CMP_FLEX_SDK / CMP_PLAYERGLOBAL / CMP_JAVA to override paths.
echo     Java 8-21 required on PATH for interface\swf\build.ps1
exit /b 0
