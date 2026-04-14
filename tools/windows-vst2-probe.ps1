param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$VstPath,

  [switch]$OpenEditor,
  [switch]$RectTwice,
  [ValidateSet("child", "top", "none")]
  [string]$Parent = "child",
  [ValidateSet("current", "worker")]
  [string]$LoadThread = "current",
  [ValidateSet("current", "worker")]
  [string]$Thread = "current",
  [int]$IdleMs = 1000,
  [int]$LoadTimeoutMs = 5000,
  [int]$OpenTimeoutMs = 2000
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$probeExe = Join-Path $repoRoot "build-win\Debug\windows-vst2-probe.exe"
if (-not (Test-Path $probeExe)) {
  throw "probe executable not found: $probeExe"
}
if (-not (Test-Path $VstPath)) {
  throw "VST path not found: $VstPath"
}

$args = @($VstPath)
if ($OpenEditor) { $args += "--open-editor" }
if ($RectTwice) { $args += "--rect-twice" }
$args += @("--parent", $Parent,
  "--load-thread", $LoadThread,
  "--thread", $Thread,
  "--idle-ms", "$IdleMs",
  "--load-timeout-ms", "$LoadTimeoutMs",
  "--open-timeout-ms", "$OpenTimeoutMs")

Write-Host "probe_exe=$probeExe"
Write-Host "vst_path=$VstPath"
Write-Host "parent_mode=$Parent"
Write-Host "load_thread_mode=$LoadThread"
Write-Host "thread_mode=$Thread"
Write-Host "open_editor=" + ($(if ($OpenEditor) { 1 } else { 0 }))

& $probeExe @args
$exitCode = $LASTEXITCODE
Write-Host "probe_exit=$exitCode"
exit $exitCode
