param(
  [ValidateSet("clear-log", "show-log", "run-host")]
  [string]$Mode = "show-log",

  [string]$PluginId = "keepsake.host-probe",
  [string]$ClapBundle = "",
  [switch]$OpenUi,
  [switch]$RunTransport,
  [string]$Lifecycle = "activate-first"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
$logPath = [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(), "keepsake-host-probe.log")

if ([string]::IsNullOrWhiteSpace($ClapBundle)) {
  $ClapBundle = Join-Path $repoRoot "build-win\Debug\host-probe.clap"
}

switch ($Mode) {
  "clear-log" {
    Remove-Item $logPath -Force -ErrorAction SilentlyContinue
    Write-Host "log_path=$logPath"
    Write-Host "log_cleared=1"
    exit 0
  }

  "show-log" {
    Write-Host "log_path=$logPath"
    if (Test-Path $logPath) {
      Get-Content $logPath
      exit 0
    }
    Write-Host "log_missing=1"
    exit 1
  }

  "run-host" {
    $env:KEEPSAKE_HOST_PROBE_LOG = $logPath
    $hostExe = Join-Path $repoRoot "build-win\Debug\windows-clap-host.exe"
    if (-not (Test-Path $hostExe)) {
      throw "windows-clap-host.exe not found: $hostExe"
    }
    $args = @(
      $PluginId,
      "--mode", $Lifecycle,
      "--scale", "2"
    )
    $args = @($ClapBundle) + $args
    if ($OpenUi) {
      $args += "--open-ui"
    }
    if ($RunTransport) {
      $args += "--run-transport"
    }
    & $hostExe @args
    $code = $LASTEXITCODE
    Write-Host "log_path=$logPath"
    exit $code
  }
}
