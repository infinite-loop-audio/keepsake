param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$VstPath,

  [switch]$OpenEditor,
  [switch]$RectTwice,
  [switch]$Activate,
  [switch]$GetChunk,
  [switch]$SetChunk,
  [switch]$ChunkDuringProcess,
  [switch]$OpenDuringProcess,
  [switch]$EditorChunkDuringProcess,
  [switch]$BridgeHostMode,
  [switch]$BridgeGateMode,
  [switch]$BridgeMarshalMode,
  [switch]$AsyncOpenMarshalMode,
  [switch]$HostResizeSweep,
  [switch]$HostResizeChild,
  [ValidateSet("child", "top", "none")]
  [string]$Parent = "child",
  [ValidateSet("current", "worker")]
  [string]$LoadThread = "current",
  [ValidateSet("current", "worker")]
  [string]$RectThread = "current",
  [ValidateSet("current", "worker")]
  [string]$OpenThread = "current",
  [ValidateSet("current", "worker")]
  [string]$ProcessThread = "current",
  [ValidateSet("current", "worker")]
  [string]$ChunkThread = "current",
  [int]$IdleMs = 1000,
  [int]$LoadTimeoutMs = 5000,
  [int]$RectTimeoutMs = 5000,
  [int]$OpenTimeoutMs = 2000,
  [int]$ProcessTimeoutMs = 5000,
  [int]$ChunkTimeoutMs = 5000,
  [int]$ProcessBlocks = 0,
  [int]$ProcessSleepMs = 0,
  [int]$SampleRate = 44100,
  [int]$BlockSize = 512,
  [int]$CallbackDelayMs = 0,
  [int]$GetTimeDelayMs = 0,
  [int]$GateDelayMs = 0,
  [int]$ResizeStepMs = 400
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
if ($Activate) { $args += "--activate" }
if ($GetChunk) { $args += "--get-chunk" }
if ($SetChunk) { $args += "--set-chunk" }
if ($ChunkDuringProcess) { $args += "--chunk-during-process" }
if ($OpenDuringProcess) { $args += "--open-during-process" }
if ($EditorChunkDuringProcess) { $args += "--editor-chunk-during-process" }
if ($BridgeHostMode) { $args += "--bridge-host-mode" }
if ($BridgeGateMode) { $args += "--bridge-gate-mode" }
if ($BridgeMarshalMode) { $args += "--bridge-marshal-mode" }
if ($AsyncOpenMarshalMode) { $args += "--async-open-marshal-mode" }
if ($HostResizeSweep) { $args += "--host-resize-sweep" }
if ($HostResizeChild) { $args += "--host-resize-child" }
$args += @("--parent", $Parent,
  "--load-thread", $LoadThread,
  "--rect-thread", $RectThread,
  "--open-thread", $OpenThread,
  "--process-thread", $ProcessThread,
  "--chunk-thread", $ChunkThread,
  "--idle-ms", "$IdleMs",
  "--load-timeout-ms", "$LoadTimeoutMs",
  "--rect-timeout-ms", "$RectTimeoutMs",
  "--open-timeout-ms", "$OpenTimeoutMs",
  "--process-timeout-ms", "$ProcessTimeoutMs",
  "--chunk-timeout-ms", "$ChunkTimeoutMs",
  "--process-blocks", "$ProcessBlocks",
  "--process-sleep-ms", "$ProcessSleepMs",
  "--sample-rate", "$SampleRate",
  "--block-size", "$BlockSize",
  "--callback-delay-ms", "$CallbackDelayMs",
  "--get-time-delay-ms", "$GetTimeDelayMs",
  "--gate-delay-ms", "$GateDelayMs",
  "--resize-step-ms", "$ResizeStepMs")

Write-Host "probe_exe=$probeExe"
Write-Host "vst_path=$VstPath"
Write-Host "parent_mode=$Parent"
Write-Host "load_thread_mode=$LoadThread"
Write-Host "rect_thread_mode=$RectThread"
Write-Host "open_thread_mode=$OpenThread"
Write-Host "process_thread_mode=$ProcessThread"
Write-Host "chunk_thread_mode=$ChunkThread"
Write-Host "open_editor=" + ($(if ($OpenEditor) { 1 } else { 0 }))
Write-Host "activate=" + ($(if ($Activate) { 1 } else { 0 }))
Write-Host "get_chunk=" + ($(if ($GetChunk) { 1 } else { 0 }))
Write-Host "set_chunk=" + ($(if ($SetChunk) { 1 } else { 0 }))
Write-Host "chunk_during_process=" + ($(if ($ChunkDuringProcess) { 1 } else { 0 }))
Write-Host "open_during_process=" + ($(if ($OpenDuringProcess) { 1 } else { 0 }))
Write-Host "editor_chunk_during_process=" + ($(if ($EditorChunkDuringProcess) { 1 } else { 0 }))
Write-Host "bridge_host_mode=" + ($(if ($BridgeHostMode) { 1 } else { 0 }))
Write-Host "bridge_gate_mode=" + ($(if ($BridgeGateMode) { 1 } else { 0 }))
Write-Host "bridge_marshal_mode=" + ($(if ($BridgeMarshalMode) { 1 } else { 0 }))
Write-Host "async_open_marshal_mode=" + ($(if ($AsyncOpenMarshalMode) { 1 } else { 0 }))
Write-Host "host_resize_sweep=" + ($(if ($HostResizeSweep) { 1 } else { 0 }))
Write-Host "host_resize_child=" + ($(if ($HostResizeChild) { 1 } else { 0 }))
Write-Host "process_blocks=$ProcessBlocks"
Write-Host "sample_rate=$SampleRate"
Write-Host "block_size=$BlockSize"
Write-Host "callback_delay_ms=$CallbackDelayMs"
Write-Host "get_time_delay_ms=$GetTimeDelayMs"
Write-Host "gate_delay_ms=$GateDelayMs"
Write-Host "resize_step_ms=$ResizeStepMs"

& $probeExe @args
$exitCode = $LASTEXITCODE
Write-Host "probe_exit=$exitCode"
exit $exitCode
