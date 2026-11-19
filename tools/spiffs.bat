@echo off
rem ---------------------------------------------------------------
rem  spiffs.bat - build spiffs.bin (SPIFFS image) for the receiver
rem  Double-click to build firmware\WT32_ETH01_receiver\spiffs.bin,
rem  or pass extra arguments, e.g.:
rem      spiffs.bat -Data firmware\ESP32_D1_mini_rc\data
rem      spiffs.bat -Size 0x100000 -Out C:\tmp\spiffs.bin
rem  Needs the ESP32 Arduino core (bundles mkspiffs.exe).
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
