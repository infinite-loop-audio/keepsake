param(
  [string]$TaskName = "KeepsakeInstallBuilt",
  [string]$SourceBundle = "",
  [string]$TargetBundle = "",
  [switch]$ElevatedPassThru
)

$ErrorActionPreference = "Stop"

function Test-IsAdministrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

if ([string]::IsNullOrWhiteSpace($SourceBundle)) {
  $SourceBundle = Join-Path $repoRoot "build-win\Debug\keepsake.clap"
}
if ([string]::IsNullOrWhiteSpace($TargetBundle)) {
  $TargetBundle = Join-Path ${env:CommonProgramFiles} "CLAP\keepsake.clap"
}

$SourceBundle = [System.IO.Path]::GetFullPath($SourceBundle)
$TargetBundle = [System.IO.Path]::GetFullPath($TargetBundle)
$installScript = Join-Path $scriptDir "windows-install-built.ps1"

if (-not (Test-Path $installScript)) {
  throw "Install script not found: $installScript"
}

if (-not (Test-IsAdministrator)) {
  $argList = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", "`"$PSCommandPath`"",
    "-TaskName", "`"$TaskName`"",
    "-SourceBundle", "`"$SourceBundle`"",
    "-TargetBundle", "`"$TargetBundle`"",
    "-ElevatedPassThru"
  )

  $proc = Start-Process -FilePath "powershell.exe" -ArgumentList $argList -Verb RunAs -Wait -PassThru
  exit $proc.ExitCode
}

$currentUser = [Security.Principal.WindowsIdentity]::GetCurrent().Name
$actionArgs = @(
  "-NoProfile",
  "-ExecutionPolicy", "Bypass",
  "-File", "`"$installScript`"",
  "-SourceBundle", "`"$SourceBundle`"",
  "-TargetBundle", "`"$TargetBundle`""
)

$action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument ($actionArgs -join " ")
$principal = New-ScheduledTaskPrincipal -UserId $currentUser -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet `
  -AllowStartIfOnBatteries `
  -DontStopIfGoingOnBatteries `
  -StartWhenAvailable `
  -MultipleInstances IgnoreNew

Register-ScheduledTask `
  -TaskName $TaskName `
  -Action $action `
  -Principal $principal `
  -Settings $settings `
  -Description "Install the latest Keepsake CLAP bundle and bridge binary into Common Files\\CLAP with elevated privileges." `
  -Force | Out-Null

$task = Get-ScheduledTask -TaskName $TaskName
$info = Get-ScheduledTaskInfo -TaskName $TaskName

Write-Host "task_name=$TaskName"
Write-Host "task_user=$currentUser"
Write-Host "task_state=$($task.State)"
Write-Host "last_result=$($info.LastTaskResult)"
Write-Host "source_bundle=$SourceBundle"
Write-Host "target_bundle=$TargetBundle"
