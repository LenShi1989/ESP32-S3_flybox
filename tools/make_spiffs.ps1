<#
  make_spiffs.ps1 - Build a SPIFFS image (spiffs.bin) from a sketch data folder.

  Default target:
      firmware\WT32_ETH01_receiver\data  ->  build\spiffs.bin

  Relative -Data / -Out paths are resolved against the repository root.

  Default partition (ESP32 core "default" 4MB layout used by WT32-ETH01):
      spiffs  offset 0x290000  size 0x160000 (1.5 MB)

  Usage:
      tools\spiffs.bat
      tools\spiffs.bat -Data firmware\ESP32_D1_mini_rc\data
      tools\spiffs.bat -Size 0x100000 -Out C:\tmp\spiffs.bin
      tools\spiffs.bat -Out build\rc-spiffs.bin
      tools\spiffs.bat -Mkspiffs "C:\path\to\mkspiffs.exe"
#>
[CmdletBinding()]
param(
  [string]$Data,
  [string]$Out,
  [string]$Size = '0x160000',
  [string]$Mkspiffs
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Fail([string]$msg) {
  Write-Host "[ERROR] $msg" -ForegroundColor Red
  exit 1
}

if (-not $Data) { $Data = Join-Path $repo 'firmware\WT32_ETH01_receiver\data' }
elseif (-not [System.IO.Path]::IsPathRooted($Data)) { $Data = Join-Path $repo $Data }
if (-not (Test-Path $Data)) { Fail "data folder not found: $Data" }
$Data = (Resolve-Path $Data).Path
if (-not $Out) { $Out = Join-Path $repo 'build\spiffs.bin' }
elseif (-not [System.IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $repo $Out }
$Out = [System.IO.Path]::GetFullPath($Out)
$outDir = Split-Path -Parent $Out
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

# ---------- locate mkspiffs.exe ----------
$cands = @()
if ($Mkspiffs)    { $cands += $Mkspiffs }
if ($env:MKSPIFFS) { $cands += $env:MKSPIFFS }
foreach ($base in @("$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\mkspiffs",
                    "$env:USERPROFILE\AppData\Local\Arduino15\packages\esp32\tools\mkspiffs")) {
  if (Test-Path $base) {
    $cands += @(Get-ChildItem $base -Recurse -Filter mkspiffs.exe -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending | ForEach-Object { $_.FullName })
  }
}
$exe = $null
foreach ($c in $cands) { if ($c -and (Test-Path $c)) { $exe = (Resolve-Path $c).Path; break } }
if (-not $exe) {
  Fail "mkspiffs.exe not found. Install the ESP32 core in Arduino IDE, or pass -Mkspiffs <path> / set MKSPIFFS."
}

# ---------- resolve partition size ----------
if ($Size -match '^0x([0-9a-fA-F]+)$') { $sizeNum = [Convert]::ToInt64($Matches[1], 16) }
else { $sizeNum = [int64]$Size }
if ($sizeNum -le 0) { Fail "invalid -Size: $Size" }

# ---------- check data size ----------
$files = @(Get-ChildItem $Data -Recurse -File)
$dataBytes = ($files | Measure-Object -Property Length -Sum).Sum
if (-not $dataBytes) { $dataBytes = 0 }
if ($dataBytes -gt $sizeNum) {
  Fail ("data is {0} bytes but the partition is only {1} bytes" -f $dataBytes, $sizeNum)
}

Write-Host "mkspiffs : $exe"
Write-Host "data dir : $Data ($($files.Count) files, $dataBytes bytes)"
Write-Host "output   : $Out"
Write-Host ("size     : 0x{0:X} ({1} bytes)" -f $sizeNum, $sizeNum)

if (Test-Path $Out) { Remove-Item $Out -Force }

& $exe -c $Data -b 4096 -p 256 -s $sizeNum $Out
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $Out)) { Fail "mkspiffs failed (exit $LASTEXITCODE)" }

$imgBytes = (Get-Item $Out).Length
Write-Host ("[OK] {0} ({1} bytes, data usage {2}%)" -f $Out, $imgBytes, [math]::Round($dataBytes * 100 / $sizeNum, 1)) -ForegroundColor Green
Write-Host ""
Write-Host "OTA upload : receiver web page -> OTA -> 'spiffs.bin' -> upload (auto reboot)"
Write-Host "USB flash  : esptool --chip esp32 --port COMx write_flash 0x290000 build\spiffs.bin"
