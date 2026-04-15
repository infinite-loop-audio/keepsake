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
  [int]$ActivateDelayMs = 0,
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

& $exe @args
exit $LASTEXITCODE
