param(
  [string]$TaskName = "KeepsakeInstallBuilt",
  [int]$TimeoutSec = 120
)

$ErrorActionPreference = "Stop"

$task = Get-ScheduledTask -TaskName $TaskName -ErrorAction Stop
$before = Get-ScheduledTaskInfo -TaskName $TaskName
$beforeRun = $before.LastRunTime
$beforeResult = $before.LastTaskResult

Start-ScheduledTask -TaskName $TaskName

$deadline = (Get-Date).AddSeconds($TimeoutSec)
do {
  Start-Sleep -Milliseconds 400
  $info = Get-ScheduledTaskInfo -TaskName $TaskName
  $task = Get-ScheduledTask -TaskName $TaskName
  $ran = ($info.LastRunTime -gt $beforeRun) -or ($beforeRun.Year -le 1900 -and $info.LastRunTime.Year -gt 1900)
  $finished = $ran -and $task.State -ne "Running" -and $task.State -ne "Queued"
  if ($finished) {
    Write-Host "task_name=$TaskName"
    Write-Host "task_state=$($task.State)"
    Write-Host "last_run_time=$($info.LastRunTime.ToString("o"))"
    Write-Host "last_result=$($info.LastTaskResult)"
    if ($info.LastTaskResult -ne 0) {
      throw "Scheduled task failed with result $($info.LastTaskResult)"
    }
    exit 0
  }
} while ((Get-Date) -lt $deadline)

$info = Get-ScheduledTaskInfo -TaskName $TaskName
$task = Get-ScheduledTask -TaskName $TaskName
throw "Timed out waiting for scheduled task '$TaskName' to finish (state=$($task.State) last_run=$($info.LastRunTime.ToString("o")) last_result=$($info.LastTaskResult) previous_result=$beforeResult)"
