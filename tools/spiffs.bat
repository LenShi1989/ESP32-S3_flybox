@echo off
rem ---------------------------------------------------------------
rem  spiffs.bat - build spiffs.bin (SPIFFS image) for the Flybox boards
rem
rem  Default (receiver):
rem      tools\spiffs.bat
rem      -> firmware\WT32_ETH01_receiver\build\spiffs.bin
rem
rem  All boards (receiver + remote controller + flybox):
rem      tools\spiffs.bat -All
rem      -> firmware\WT32_ETH01_receiver\build\spiffs.bin
rem      -> firmware\ESP32_D1_mini_rc\build\spiffs.bin
rem      -> firmware\ESP32_S3_flybox\build\spiffs.bin
rem
rem  ESP32-S3 Flybox only:
rem      tools\spiffs.bat -Flybox
rem      -> firmware\ESP32_S3_flybox\build\spiffs.bin
rem
rem  Other options:
rem      tools\spiffs.bat -Data firmware\ESP32_D1_mini_rc\data
rem      tools\spiffs.bat -Size 0x100000 -Out C:\tmp\spiffs.bin
rem      tools\spiffs.bat -Mkspiffs "C:\path\to\mkspiffs.exe"
rem
rem  Needs the ESP32 Arduino core (bundles mkspiffs.exe).
rem  Partition size follows the table the sketch was actually built with
rem  (<sketch>\partitions.csv or <sketch>\build\*\partitions.csv), so
rem  build the sketch once before running this.
rem ---------------------------------------------------------------
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_spiffs.ps1" %*
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
  echo.
  echo [FAILED] spiffs.bin was not created.
  if "%~1"=="" pause
  exit /b %RC%
)
if "%~1"=="" pause
exit /b 0
