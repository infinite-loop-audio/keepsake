param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$PluginId,

  [string]$ClapBundle = "",
  [string]$VstPath = "",
  [string]$Mode = "gui-first",
  [switch]$OpenUi,
  [switch]$RunTransport,
  [switch]$ActivateBeforeUi,
  [switch]$ShowParentBeforeSetParent,
  [switch]$ReaperParentShape,
  [switch]$ProcessOffMainThread,
  [switch]$PromoteParentAfterShow,
  [switch]$QueryReaperExtensions,
  [switch]$RestartProcessingAfterUi,
  [int]$StateSavesBeforeUi = -1,
  [int]$StateSavesAfterUi = -1,
  [int]$ProcessThreadCount = -1,
  [int]$RestartProcessBlocks = -1,
  [int]$ActivateDelayMs = 0,
  [int]$ParentPromoteDelayMs = 50,
  [int]$UiTimeoutMs = 5000,
  [int]$ProcessBlocks = 96,
  [double]$Scale = 2.0
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

if ([string]::IsNullOrWhiteSpace($ClapBundle)) {
  $ClapBundle = Join-Path $repoRoot "build-win\Debug\keepsake.clap"
}

$exe = Join-Path $repoRoot "build-win\Debug\windows-clap-host.exe"
if (-not (Test-Path $exe)) {
  throw "windows-clap-host.exe not found: $exe"
}

$args = @($ClapBundle, $PluginId, "--mode", $Mode, "--ui-timeout-ms", "$UiTimeoutMs", "--process-blocks", "$ProcessBlocks", "--scale", "$Scale")
if (-not [string]::IsNullOrWhiteSpace($VstPath)) {
  $args += @("--vst-path", $VstPath)
}
if ($ActivateDelayMs -gt 0) {
  $args += @("--activate-delay-ms", "$ActivateDelayMs")
}
if ($QueryReaperExtensions) {
  $args += "--query-reaper-extensions"
}
if ($RestartProcessingAfterUi) {
  $args += "--restart-processing-after-ui"
}
if ($StateSavesBeforeUi -ge 0) {
  $args += @("--state-saves-before-ui", "$StateSavesBeforeUi")
}
if ($StateSavesAfterUi -ge 0) {
  $args += @("--state-saves-after-ui", "$StateSavesAfterUi")
}
if ($ProcessThreadCount -ge 0) {
  $args += @("--process-thread-count", "$ProcessThreadCount")
}
if ($RestartProcessBlocks -ge 0) {
  $args += @("--restart-process-blocks", "$RestartProcessBlocks")
}
if ($OpenUi) {
  $args += "--open-ui"
}
if ($RunTransport) {
  $args += "--run-transport"
}
if ($ActivateBeforeUi) {
  $args += "--activate-before-ui"
}
if ($ShowParentBeforeSetParent) {
  $args += "--show-parent-before-set-parent"
}
if ($ReaperParentShape) {
  $args += "--reaper-parent-shape"
}
if ($ProcessOffMainThread) {
  $args += "--process-off-main-thread"
}
if ($PromoteParentAfterShow) {
  $args += "--promote-parent-after-show"
  $args += @("--parent-promote-delay-ms", "$ParentPromoteDelayMs")
}

& $exe @args
exit $LASTEXITCODE
