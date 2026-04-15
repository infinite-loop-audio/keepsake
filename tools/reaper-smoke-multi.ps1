param(
  [Parameter(Mandatory = $true)]
  [string[]]$PluginIds,

  [Parameter(Mandatory = $true)]
  [string[]]$VstPaths,

  [string]$ClapBundle = "",
  [string]$ClapPathOverride = "",
  [string]$BridgePathOverride = "",
  [string]$EmbedModeOverride = "",
  [string]$Reaper = "C:\Program Files\REAPER (x64)\reaper.exe",
  [int]$TimeoutSec = 90,
  [int]$ScanTimeoutMs = 20000,
  [int]$SettleMs = 1500,
  [switch]$OpenUi,
  [int]$UiTimeoutMs = 5000,
  [switch]$RunTransport,
  [switch]$RequireAudio,
  [double]$MinPeak = 0.00001,
  [int]$PlayTimeoutMs = 5000,
  [int]$PlayHoldMs = 1000,
  [switch]$KeepTemp,
  [switch]$UseDefaultConfig
)

$ErrorActionPreference = "Stop"

if ($PluginIds.Count -ne $VstPaths.Count) {
  throw "PluginIds and VstPaths must have the same number of entries"
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
if ([string]::IsNullOrWhiteSpace($ClapBundle)) {
  $ClapBundle = Join-Path $repoRoot "build-win\Debug\keepsake.clap"
}

$scriptPath = Join-Path $repoRoot "tools\reaper-smoke-multi.lua"
$defaultReaperClapIni = Join-Path $env:APPDATA "REAPER\reaper-clap-win64.ini"
if (-not (Test-Path $Reaper)) { throw "REAPER binary not found: $Reaper" }
if (-not (Test-Path $ClapBundle)) { throw "CLAP bundle not found: $ClapBundle" }
foreach ($vst in $VstPaths) {
  if (-not (Test-Path $vst)) { throw "VST path not found: $vst" }
}

$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("keepsake-reaper-smoke." + [System.Guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory -Path $tmpDir -Force
$cfgIni = Join-Path $tmpDir "reaper.ini"
$statusFile = Join-Path $tmpDir "status.txt"
$logFile = Join-Path $tmpDir "reaper-smoke.log"
$stdoutFile = Join-Path $tmpDir "reaper.stdout.log"
$stderrFile = Join-Path $tmpDir "reaper.stderr.log"
$debugLogCopy = Join-Path $tmpDir "keepsake-debug.log"
$vstScanDir = Join-Path $tmpDir "vst-targets"
$emptyVstDir = Join-Path $tmpDir "empty-vst"
$cfgArgs = @()
$debugLogSource = Join-Path $env:TEMP "keepsake-debug.log"
$preserveTemp = $KeepTemp

$null = New-Item -ItemType Directory -Path $vstScanDir -Force
$null = New-Item -ItemType Directory -Path $emptyVstDir -Force

foreach ($vst in $VstPaths) {
  Copy-Item -LiteralPath $vst -Destination (Join-Path $vstScanDir ([System.IO.Path]::GetFileName($vst))) -Force
}

try {
  if (-not $UseDefaultConfig) {
    $clapPath = Split-Path -Parent $ClapBundle
    @"
[REAPER]
multinst=1
splash=0
splash2=0
splashfast=1
newprojtmpl=
vstpath64=$emptyVstDir
clap_path_win64=$clapPath
"@ | Set-Content -LiteralPath $cfgIni -Encoding ASCII
    $cfgArgs = @("-cfgfile", $cfgIni)
  }

  Write-Host ("plugin_ids=" + ($PluginIds -join ";"))
  Write-Host ("vst_paths=" + ($VstPaths -join ";"))
  Write-Host "clap_bundle=$ClapBundle"
  Write-Host "temp_dir=$tmpDir"
  Write-Host "log_file=$logFile"
  Write-Host "debug_log=$debugLogCopy"
  Write-Host ("config_mode=" + ($(if ($UseDefaultConfig) { "default" } else { "isolated" })))
  Write-Host ("open_ui=" + ($(if ($OpenUi) { 1 } else { 0 })))
  Write-Host ("run_transport=" + ($(if ($RunTransport) { 1 } else { 0 })))
  Write-Host ("require_audio=" + ($(if ($RequireAudio) { 1 } else { 0 })))

  $old = @{
    CLAP_PATH = $env:CLAP_PATH
    KEEPSAKE_BRIDGE_PATH = $env:KEEPSAKE_BRIDGE_PATH
    KEEPSAKE_WIN_EMBED_MODE = $env:KEEPSAKE_WIN_EMBED_MODE
    KEEPSAKE_VST2_PATH = $env:KEEPSAKE_VST2_PATH
    KEEPSAKE_REAPER_SMOKE_PLUGIN_IDS = $env:KEEPSAKE_REAPER_SMOKE_PLUGIN_IDS
    KEEPSAKE_REAPER_SMOKE_LOG = $env:KEEPSAKE_REAPER_SMOKE_LOG
    KEEPSAKE_REAPER_SMOKE_STATUS = $env:KEEPSAKE_REAPER_SMOKE_STATUS
    KEEPSAKE_REAPER_SMOKE_SCAN_TIMEOUT_MS = $env:KEEPSAKE_REAPER_SMOKE_SCAN_TIMEOUT_MS
    KEEPSAKE_REAPER_SMOKE_SETTLE_MS = $env:KEEPSAKE_REAPER_SMOKE_SETTLE_MS
    KEEPSAKE_REAPER_SMOKE_OPEN_UI = $env:KEEPSAKE_REAPER_SMOKE_OPEN_UI
    KEEPSAKE_REAPER_SMOKE_UI_TIMEOUT_MS = $env:KEEPSAKE_REAPER_SMOKE_UI_TIMEOUT_MS
    KEEPSAKE_REAPER_SMOKE_RUN_TRANSPORT = $env:KEEPSAKE_REAPER_SMOKE_RUN_TRANSPORT
    KEEPSAKE_REAPER_SMOKE_REQUIRE_AUDIO = $env:KEEPSAKE_REAPER_SMOKE_REQUIRE_AUDIO
    KEEPSAKE_REAPER_SMOKE_MIN_PEAK = $env:KEEPSAKE_REAPER_SMOKE_MIN_PEAK
    KEEPSAKE_REAPER_SMOKE_PLAY_TIMEOUT_MS = $env:KEEPSAKE_REAPER_SMOKE_PLAY_TIMEOUT_MS
    KEEPSAKE_REAPER_SMOKE_PLAY_HOLD_MS = $env:KEEPSAKE_REAPER_SMOKE_PLAY_HOLD_MS
  }

  $env:CLAP_PATH = $(if ([string]::IsNullOrWhiteSpace($ClapPathOverride)) { "" } else { $ClapPathOverride })
  $env:KEEPSAKE_BRIDGE_PATH = $(if ([string]::IsNullOrWhiteSpace($BridgePathOverride)) { "" } else { $BridgePathOverride })
  $env:KEEPSAKE_WIN_EMBED_MODE = $(if ([string]::IsNullOrWhiteSpace($EmbedModeOverride)) { "" } else { $EmbedModeOverride })
  $env:KEEPSAKE_VST2_PATH = $vstScanDir
  $env:KEEPSAKE_REAPER_SMOKE_PLUGIN_IDS = ($PluginIds -join ";")
  $env:KEEPSAKE_REAPER_SMOKE_LOG = $logFile
  $env:KEEPSAKE_REAPER_SMOKE_STATUS = $statusFile
  $env:KEEPSAKE_REAPER_SMOKE_SCAN_TIMEOUT_MS = "$ScanTimeoutMs"
  $env:KEEPSAKE_REAPER_SMOKE_SETTLE_MS = "$SettleMs"
  $env:KEEPSAKE_REAPER_SMOKE_OPEN_UI = $(if ($OpenUi) { "1" } else { "0" })
  $env:KEEPSAKE_REAPER_SMOKE_UI_TIMEOUT_MS = "$UiTimeoutMs"
  $env:KEEPSAKE_REAPER_SMOKE_RUN_TRANSPORT = $(if ($RunTransport) { "1" } else { "0" })
  $env:KEEPSAKE_REAPER_SMOKE_REQUIRE_AUDIO = $(if ($RequireAudio) { "1" } else { "0" })
  $env:KEEPSAKE_REAPER_SMOKE_MIN_PEAK = "$MinPeak"
  $env:KEEPSAKE_REAPER_SMOKE_PLAY_TIMEOUT_MS = "$PlayTimeoutMs"
  $env:KEEPSAKE_REAPER_SMOKE_PLAY_HOLD_MS = "$PlayHoldMs"

  try {
    Remove-Item -LiteralPath $debugLogSource -Force -ErrorAction SilentlyContinue
    $reaperArgs = @("-newinst", "-nosplash", "-new") + $cfgArgs + @($scriptPath)
    $proc = Start-Process -FilePath $Reaper `
      -ArgumentList $reaperArgs `
      -PassThru `
      -RedirectStandardOutput $stdoutFile `
      -RedirectStandardError $stderrFile `
      -WorkingDirectory (Split-Path -Parent $Reaper)

    $procId = $proc.Id
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $result = ""

    while ($true) {
      if (Test-Path $statusFile) {
        $result = (Get-Content -LiteralPath $statusFile -Raw).Trim()
        break
      }
      if ((Get-Date) -ge $deadline) {
        $result = "TIMEOUT"
        break
      }
      try {
        $null = Get-Process -Id $procId -ErrorAction Stop
      } catch {
        break
      }
      Start-Sleep -Seconds 1
    }
  } finally {
    foreach ($k in $old.Keys) {
      if ($null -eq $old[$k]) {
        Remove-Item "Env:$k" -ErrorAction SilentlyContinue
      } else {
        Set-Item "Env:$k" $old[$k]
      }
    }
  }

  try {
    Stop-Process -Id $procId -ErrorAction SilentlyContinue
  } catch {}

  if (Test-Path $debugLogSource) {
    Copy-Item -LiteralPath $debugLogSource -Destination $debugLogCopy -Force
  }

  Write-Host ""
  if (Test-Path $logFile) {
    Get-Content -LiteralPath $logFile
  } else {
    Write-Host "no smoke log was produced"
  }
  Write-Host ""
  Write-Host "reaper_stdout=$stdoutFile"
  Write-Host "reaper_stderr=$stderrFile"
  Write-Host "debug_log=$debugLogCopy"

  if ($result -eq "PASS") {
    Write-Host "result=PASS"
    exit 0
  }
  if ($result -eq "FAIL") {
    Write-Host "result=FAIL"
    exit 1
  }
  if ($result -eq "TIMEOUT") {
    Write-Host "result=TIMEOUT"
    exit 124
  }

  Write-Host "result=UNKNOWN"
  exit 1
}
finally {
  if (-not $preserveTemp) {
    Remove-Item -LiteralPath $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
  } else {
    Write-Host "temp dir preserved at $tmpDir"
  }
}
