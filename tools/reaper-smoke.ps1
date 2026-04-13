param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$PluginId,

  [Parameter(Mandatory = $true)]
  [string]$VstPath,

  [string]$ClapBundle = "",
  [string]$Reaper = "C:\Program Files\REAPER (x64)\reaper.exe",
  [int]$TimeoutSec = 45,
  [int]$ScanTimeoutMs = 15000,
  [int]$SettleMs = 1500,
  [switch]$OpenUi,
  [int]$UiTimeoutMs = 5000,
  [switch]$RunTransport,
  [switch]$RequireAudio,
  [double]$MinPeak = 0.00001,
  [int]$PlayTimeoutMs = 5000,
  [int]$PlayHoldMs = 1000,
  [switch]$KeepTemp,
  [switch]$UseDefaultConfig,
  [switch]$SyncDefaultInstall
)

$ErrorActionPreference = "Stop"

function Restore-ReaperClapCache {
  param(
    [string]$BackupPath,
    [string]$TargetPath
  )

  if (-not (Test-Path $BackupPath)) { return }

  $sectionName = "keepsake.clap"

  function Parse-IniSections {
    param([string]$Text)

    $normalized = ($Text -replace "`r`n", "`n" -replace "`r", "`n")
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($line in ($normalized -split "`n")) {
      $lines.Add($line + "`n")
    }
    if ($normalized.Length -eq 0) {
      return @()
    }

    $sections = [System.Collections.Generic.List[object]]::new()
    $currentName = $null
    $currentLines = [System.Collections.Generic.List[string]]::new()

    foreach ($line in $lines) {
      if ($line.StartsWith("[") -and $line.Contains("]")) {
        if ($null -ne $currentName) {
          $sections.Add([pscustomobject]@{
            Name  = $currentName
            Lines = @($currentLines.ToArray())
          })
        }
        $currentName = $line.Trim().TrimStart("[").TrimEnd("]")
        $currentLines = [System.Collections.Generic.List[string]]::new()
        $currentLines.Add($line)
      } elseif ($null -eq $currentName) {
        $sections.Add([pscustomobject]@{
          Name  = $null
          Lines = @($line)
        })
      } else {
        $currentLines.Add($line)
      }
    }

    if ($null -ne $currentName) {
      $sections.Add([pscustomobject]@{
        Name  = $currentName
        Lines = @($currentLines.ToArray())
      })
    }

    return @($sections.ToArray())
  }

  $backupText = if (Test-Path $BackupPath) {
    Get-Content -LiteralPath $BackupPath -Raw
  } else {
    ""
  }
  $targetText = if (Test-Path $TargetPath) {
    Get-Content -LiteralPath $TargetPath -Raw
  } else {
    ""
  }

  $backupSections = Parse-IniSections -Text $backupText
  $targetSections = Parse-IniSections -Text $targetText
  $backupKeepsake = $backupSections | Where-Object { $_.Name -eq $sectionName } | Select-Object -First 1

  $result = [System.Collections.Generic.List[string]]::new()
  $inserted = $false

  foreach ($section in $targetSections) {
    if ($section.Name -eq $sectionName) {
      if ($null -ne $backupKeepsake) {
        foreach ($line in $backupKeepsake.Lines) { $result.Add($line) }
        $inserted = $true
      }
      continue
    }
    foreach ($line in $section.Lines) { $result.Add($line) }
  }

  if (($null -ne $backupKeepsake) -and (-not $inserted)) {
    if (($result.Count -gt 0) -and ($result[$result.Count - 1] -ne "`n")) {
      if (-not $result[$result.Count - 1].EndsWith("`n")) {
        $result[$result.Count - 1] += "`n"
      }
      $result.Add("`n")
    }
    foreach ($line in $backupKeepsake.Lines) { $result.Add($line) }
  }

  $targetDir = Split-Path -Parent $TargetPath
  if (-not [string]::IsNullOrWhiteSpace($targetDir)) {
    $null = New-Item -ItemType Directory -Path $targetDir -Force
  }
  [System.IO.File]::WriteAllText($TargetPath, ($result -join ""))
}

function Sync-DefaultInstallBundle {
  param(
    [string]$SourceBundle,
    [string]$TargetBundle
  )

  $targetDir = Split-Path -Parent $TargetBundle
  $sourceDir = Split-Path -Parent $SourceBundle
  $sourceBridge = Join-Path $sourceDir "keepsake-bridge.exe"
  $targetBridge = Join-Path $targetDir "keepsake-bridge.exe"

  if (-not (Test-Path $sourceBridge)) {
    throw "bridge binary not found next to CLAP artifact: $sourceBridge"
  }

  $null = New-Item -ItemType Directory -Path $targetDir -Force
  Copy-Item -LiteralPath $SourceBundle -Destination $TargetBundle -Force
  Copy-Item -LiteralPath $sourceBridge -Destination $targetBridge -Force

  $stamp = Get-Date
  Get-Item -LiteralPath $TargetBundle | ForEach-Object { $_.LastWriteTime = $stamp }
  Get-Item -LiteralPath $targetBridge | ForEach-Object { $_.LastWriteTime = $stamp }
  Get-Item -LiteralPath $targetDir | ForEach-Object { $_.LastWriteTime = $stamp }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
if ([string]::IsNullOrWhiteSpace($ClapBundle)) {
  $ClapBundle = Join-Path $repoRoot "build-win\Debug\keepsake.clap"
}

$scriptPath = Join-Path $repoRoot "tools\reaper-smoke.lua"
$defaultInstallBundle = Join-Path ${env:CommonProgramFiles} "CLAP\keepsake.clap"
$defaultReaperDir = Join-Path ${env:APPDATA} "REAPER"
$defaultReaperClapIni = Join-Path $defaultReaperDir "reaper-clap-win64.ini"

if (-not (Test-Path $Reaper)) { throw "REAPER binary not found: $Reaper" }
if (-not (Test-Path $ClapBundle)) { throw "CLAP bundle not found: $ClapBundle" }
if (-not (Test-Path $VstPath)) { throw "VST path not found: $VstPath" }
if ($SyncDefaultInstall -and -not $UseDefaultConfig) {
  throw "--SyncDefaultInstall requires --UseDefaultConfig"
}

$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("keepsake-reaper-smoke." + [System.Guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory -Path $tmpDir -Force
$cfgIni = Join-Path $tmpDir "reaper.ini"
$statusFile = Join-Path $tmpDir "status.txt"
$logFile = Join-Path $tmpDir "reaper-smoke.log"
$stdoutFile = Join-Path $tmpDir "reaper.stdout.log"
$stderrFile = Join-Path $tmpDir "reaper.stderr.log"
$vstScanDir = Join-Path $tmpDir "vst-targets"
$emptyVstDir = Join-Path $tmpDir "empty-vst"
$reaperClapBackup = Join-Path $tmpDir "reaper-clap.backup.ini"
$cfgArgs = @()

$null = New-Item -ItemType Directory -Path $vstScanDir -Force
$null = New-Item -ItemType Directory -Path $emptyVstDir -Force
Copy-Item -LiteralPath $VstPath -Destination (Join-Path $vstScanDir ([System.IO.Path]::GetFileName($VstPath))) -Force

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
  } else {
    if (Test-Path $defaultReaperClapIni) {
      Copy-Item -LiteralPath $defaultReaperClapIni -Destination $reaperClapBackup -Force
    } else {
      Set-Content -LiteralPath $reaperClapBackup -Value "" -Encoding ASCII
    }

    if ($SyncDefaultInstall) {
      Sync-DefaultInstallBundle -SourceBundle $ClapBundle -TargetBundle $defaultInstallBundle
    }
  }

  Write-Host "plugin_id=$PluginId"
  Write-Host "vst_path=$VstPath"
  Write-Host "clap_bundle=$ClapBundle"
  Write-Host "temp_dir=$tmpDir"
  Write-Host "log_file=$logFile"
  Write-Host ("config_mode=" + ($(if ($UseDefaultConfig) { "default" } else { "isolated" })))
  Write-Host ("open_ui=" + ($(if ($OpenUi) { 1 } else { 0 })))
  Write-Host ("run_transport=" + ($(if ($RunTransport) { 1 } else { 0 })))
  Write-Host ("require_audio=" + ($(if ($RequireAudio) { 1 } else { 0 })))
  if ($SyncDefaultInstall) {
    Write-Host "default_install=$defaultInstallBundle"
  }

  $old = @{
    CLAP_PATH = $env:CLAP_PATH
    KEEPSAKE_VST2_PATH = $env:KEEPSAKE_VST2_PATH
    KEEPSAKE_REAPER_SMOKE_PLUGIN_ID = $env:KEEPSAKE_REAPER_SMOKE_PLUGIN_ID
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

  $env:CLAP_PATH = ""
  $env:KEEPSAKE_VST2_PATH = $vstScanDir
  $env:KEEPSAKE_REAPER_SMOKE_PLUGIN_ID = $PluginId
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

  Write-Host ""
  if (Test-Path $logFile) {
    Get-Content -LiteralPath $logFile
  } else {
    Write-Host "no smoke log was produced"
  }
  Write-Host ""
  Write-Host "reaper_stdout=$stdoutFile"
  Write-Host "reaper_stderr=$stderrFile"

  switch ($result) {
    "PASS" { Write-Host "result=PASS"; exit 0 }
    "FAIL" { Write-Host "result=FAIL"; exit 1 }
    "TIMEOUT" { Write-Host "result=TIMEOUT"; exit 124 }
    default {
      if ($procId) {
        Write-Host "result=UNKNOWN"
      } else {
        Write-Host "result=LAUNCH_FAILED"
      }
      exit 1
    }
  }
}
finally {
  if ($UseDefaultConfig -and (Test-Path $reaperClapBackup)) {
    Restore-ReaperClapCache -BackupPath $reaperClapBackup -TargetPath $defaultReaperClapIni
  }

  if (-not $KeepTemp) {
    Remove-Item -LiteralPath $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
  } else {
    Write-Host "temp dir preserved at $tmpDir"
  }
}
