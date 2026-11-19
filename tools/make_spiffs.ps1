<#
  make_spiffs.ps1 - Build SPIFFS images (spiffs.bin) for the Flybox sketches.

  Default output (each sketch gets its own build folder):
      firmware\WT32_ETH01_receiver\build\spiffs.bin
      firmware\ESP32_D1_mini_rc\build\spiffs.bin        (-All)

  Partition size:
      -Size auto (default)  read the "spiffs" row from <sketch>\partitions.csv,
                            then from the ESP32 core default.csv; fallback 0x160000.
      Also accepts 0x160000, 1441792, 1536k, 1M.

  Usage:
      tools\spiffs.bat                       # receiver only
      tools\spiffs.bat -All                  # receiver + RC
      tools\spiffs.bat -Data firmware\ESP32_D1_mini_rc\data
      tools\spiffs.bat -Size 0x100000 -Out C:\tmp\spiffs.bin
      tools\spiffs.bat -Mkspiffs "C:\path\to\mkspiffs.exe"
#>
[CmdletBinding()]
param(
  [string]$Data,
  [string]$Out,
  [string]$Size = 'auto',
  [string]$Mkspiffs,
  [switch]$All
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$watch = [System.Diagnostics.Stopwatch]::StartNew()

function Fail([string]$msg) {
  Write-Host "[ERROR] $msg" -ForegroundColor Red
  exit 1
}

function Resolve-RepoPath([string]$p) {
  if ([System.IO.Path]::IsPathRooted($p)) { return $p }
  return (Join-Path $repo $p)
}

function Find-Mkspiffs {
  $cands = @()
  if ($Mkspiffs)     { $cands += $Mkspiffs }
  if ($env:MKSPIFFS) { $cands += $env:MKSPIFFS }
  foreach ($base in @("$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\mkspiffs",
                      "$env:USERPROFILE\AppData\Local\Arduino15\packages\esp32\tools\mkspiffs")) {
    if (Test-Path $base) {
      $cands += @(Get-ChildItem $base -Recurse -Filter mkspiffs.exe -ErrorAction SilentlyContinue |
                  Sort-Object FullName -Descending | ForEach-Object { $_.FullName })
    }
  }
  foreach ($c in $cands) { if ($c -and (Test-Path $c)) { return (Resolve-Path $c).Path } }
  return $null
}

function Get-CsvSpiffsSize([string]$csv) {
  if (-not (Test-Path $csv)) { return $null }
  foreach ($raw in Get-Content $csv) {
    $line = $raw.Trim()
    if (-not $line -or $line.StartsWith('#')) { continue }
    $f = @($line.Split(',') | ForEach-Object { $_.Trim() })
    if ($f.Count -ge 5 -and ($f[0] -eq 'spiffs' -or $f[0] -eq 'littlefs')) { return $f[4] }
  }
  return $null
}

function Get-CorePartitionSize {
  $root = "$env:LOCALAPPDATA\Arduino15\packages\esp32\hardware\esp32"
  if (-not (Test-Path $root)) { return $null }
  $dirs = @(Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending)
  foreach ($d in $dirs) {
    $s = Get-CsvSpiffsSize (Join-Path $d.FullName 'tools\partitions\default.csv')
    if ($s) { return $s }
  }
  return $null
}

function Get-Sha256([string]$path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  $fs = [System.IO.File]::OpenRead($path)
  try { return ([BitConverter]::ToString($sha.ComputeHash($fs))).Replace('-', '') }
  finally { $fs.Close(); $sha.Dispose() }
}

function Convert-Size([string]$s) {
  if ($s -match '^0x([0-9a-fA-F]+)$') { return [int64]([Convert]::ToInt64($Matches[1], 16)) }
  if ($s -match '^([0-9]+)([kKmM])$') {
    $n = [int64]$Matches[1]
    if ($Matches[2].ToLower() -eq 'k') { return $n * 1024 }
    return $n * 1048576
  }
  return [int64]$s
}

$exe = Find-Mkspiffs
if (-not $exe) {
  Fail "mkspiffs.exe not found. Install the ESP32 core in Arduino IDE, or pass -Mkspiffs <path> / set MKSPIFFS."
}
if ($All -and ($Data -or $Out)) { Fail "-All cannot be combined with -Data or -Out" }

$jobs = @()
if ($All) {
  $jobs += [pscustomobject]@{ Name = 'WT32_ETH01_receiver'; Data = (Join-Path $repo 'firmware\WT32_ETH01_receiver\data') }
  $jobs += [pscustomobject]@{ Name = 'ESP32_D1_mini_rc';    Data = (Join-Path $repo 'firmware\ESP32_D1_mini_rc\data') }
} else {
  if (-not $Data) { $Data = 'firmware\WT32_ETH01_receiver\data' }
  $Data = Resolve-RepoPath $Data
  $jobs += [pscustomobject]@{ Name = (Split-Path -Leaf (Split-Path -Parent $Data)); Data = $Data }
}

Write-Host "mkspiffs : $exe"

$results = @()
foreach ($job in $jobs) {
  if (-not (Test-Path $job.Data)) { Fail "data folder not found: $($job.Data)" }
  $dataDir = (Resolve-Path $job.Data).Path
  $sketch  = Split-Path -Parent $dataDir

  $sizeText = $Size
  if ($sizeText -eq 'auto') {
    $sizeText = Get-CsvSpiffsSize (Join-Path $sketch 'partitions.csv')
    if (-not $sizeText) { $sizeText = Get-CorePartitionSize }
    if (-not $sizeText) { $sizeText = '0x160000' }
  }
  $sizeNum = Convert-Size $sizeText
  if ($sizeNum -le 0) { Fail "invalid -Size: $Size" }

  if ($Out -and -not $All) {
    $outPath = [System.IO.Path]::GetFullPath((Resolve-RepoPath $Out))
  } else {
    $outPath = Join-Path $sketch 'build\spiffs.bin'
  }

  $files = @(Get-ChildItem $dataDir -Recurse -File)
  $dataBytes = [int64]0
  if ($files.Count) {
    $dataBytes = [int64](($files | Measure-Object -Property Length -Sum).Sum)
  }
  if ($dataBytes -gt $sizeNum) {
    Fail ("{0}: data is {1} bytes but the partition is only {2} bytes" -f $job.Name, $dataBytes, $sizeNum)
  }

  $outDir = Split-Path -Parent $outPath
  if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }
  if (Test-Path $outPath) { Remove-Item $outPath -Force }

  Write-Host ("[{0}] {1} files, {2} bytes, partition 0x{3:X} ({3} bytes)" -f `
              $job.Name, $files.Count, $dataBytes, $sizeNum)

  & $exe -c $dataDir -b 4096 -p 256 -s $sizeNum $outPath | Out-Null
  if ($LASTEXITCODE -ne 0 -or -not (Test-Path $outPath)) {
    Fail ("{0}: mkspiffs failed (exit {1})" -f $job.Name, $LASTEXITCODE)
  }

  $img = Get-Item $outPath
  $results += [pscustomobject]@{
    Sketch = $job.Name
    Output = $outPath
    Bytes  = $img.Length
    Usage  = ('{0:N1}%' -f ($dataBytes * 100 / $sizeNum))
    Sha256 = (Get-Sha256 $outPath)
  }
}

Write-Host ""
foreach ($r in $results) {
  Write-Host ("[OK] {0}" -f $r.Output) -ForegroundColor Green
  Write-Host ("     {0:N0} bytes, data usage {1}" -f $r.Bytes, $r.Usage)
  Write-Host ("     SHA256 {0}" -f $r.Sha256)
}
Write-Host ("     elapsed {0:N1}s" -f $watch.Elapsed.TotalSeconds)
Write-Host ""
Write-Host "OTA upload : board web page -> OTA -> choose firmware.bin / spiffs.bin (auto reboot)"
Write-Host "USB flash  : esptool --chip esp32 --port COMx write_flash 0x290000 <spiffs.bin>"
