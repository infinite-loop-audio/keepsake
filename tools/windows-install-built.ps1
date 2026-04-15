param(
  [Parameter(Mandatory = $true)]
  [string]$SourceBundle,

  [string]$TargetBundle = "",

  [switch]$ElevatedPassThru
)

$ErrorActionPreference = "Stop"

function Test-IsAdministrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Copy-KeepsakeInstall {
  param(
    [string]$BundleSource,
    [string]$BundleTarget
  )

  $targetDir = Split-Path -Parent $BundleTarget
  $sourceDir = Split-Path -Parent $BundleSource
  $sourceBridge = Join-Path $sourceDir "keepsake-bridge.exe"
  $sourceBridge32 = Join-Path $sourceDir "keepsake-bridge-32.exe"
  $targetBridge = Join-Path $targetDir "keepsake-bridge.exe"
  $targetBridge32 = Join-Path $targetDir "keepsake-bridge-32.exe"

  if (-not (Test-Path $BundleSource)) {
    throw "CLAP bundle not found: $BundleSource"
  }
  if (-not (Test-Path $sourceBridge)) {
    throw "bridge binary not found next to CLAP artifact: $sourceBridge"
  }

  $null = New-Item -ItemType Directory -Path $targetDir -Force
  $bundleTemp = Join-Path $targetDir "keepsake.clap.tmp"
  $bridgeTemp = Join-Path $targetDir "keepsake-bridge.exe.tmp"
  $bridge32Temp = Join-Path $targetDir "keepsake-bridge-32.exe.tmp"

  Remove-Item -LiteralPath $bundleTemp -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $bridgeTemp -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $bridge32Temp -Force -ErrorAction SilentlyContinue

  Copy-Item -LiteralPath $BundleSource -Destination $bundleTemp -Force
  Copy-Item -LiteralPath $sourceBridge -Destination $bridgeTemp -Force
  if (Test-Path $sourceBridge32) {
    Copy-Item -LiteralPath $sourceBridge32 -Destination $bridge32Temp -Force
  }

  Remove-Item -LiteralPath $BundleTarget -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $targetBridge -Force -ErrorAction SilentlyContinue
  if (Test-Path $bridge32Temp) {
    Remove-Item -LiteralPath $targetBridge32 -Force -ErrorAction SilentlyContinue
  }

  Move-Item -LiteralPath $bundleTemp -Destination $BundleTarget -Force
  Move-Item -LiteralPath $bridgeTemp -Destination $targetBridge -Force
  if (Test-Path $bridge32Temp) {
    Move-Item -LiteralPath $bridge32Temp -Destination $targetBridge32 -Force
  }

  $sourceBundleHash = (Get-FileHash -LiteralPath $BundleSource -Algorithm SHA256).Hash
  $targetBundleHash = (Get-FileHash -LiteralPath $BundleTarget -Algorithm SHA256).Hash
  $sourceBridgeHash = (Get-FileHash -LiteralPath $sourceBridge -Algorithm SHA256).Hash
  $targetBridgeHash = (Get-FileHash -LiteralPath $targetBridge -Algorithm SHA256).Hash
  $sourceBridge32Hash = $null
  $targetBridge32Hash = $null
  if (Test-Path $sourceBridge32) {
    $sourceBridge32Hash = (Get-FileHash -LiteralPath $sourceBridge32 -Algorithm SHA256).Hash
    $targetBridge32Hash = (Get-FileHash -LiteralPath $targetBridge32 -Algorithm SHA256).Hash
  }

  if ($sourceBundleHash -ne $targetBundleHash) {
    throw "Installed CLAP hash mismatch after copy"
  }
  if ($sourceBridgeHash -ne $targetBridgeHash) {
    throw "Installed bridge hash mismatch after copy"
  }
  if ($sourceBridge32Hash -and ($sourceBridge32Hash -ne $targetBridge32Hash)) {
    throw "Installed 32-bit bridge hash mismatch after copy"
  }

  $stamp = Get-Date
  Get-Item -LiteralPath $BundleTarget | ForEach-Object { $_.LastWriteTime = $stamp }
  Get-Item -LiteralPath $targetBridge | ForEach-Object { $_.LastWriteTime = $stamp }
  if (Test-Path $targetBridge32) {
    Get-Item -LiteralPath $targetBridge32 | ForEach-Object { $_.LastWriteTime = $stamp }
  }
  Get-Item -LiteralPath $targetDir | ForEach-Object { $_.LastWriteTime = $stamp }

  Write-Host "install_bundle=$BundleTarget"
  Write-Host "install_bridge=$targetBridge"
  Write-Host "install_bundle_hash=$targetBundleHash"
  Write-Host "install_bridge_hash=$targetBridgeHash"
  if ($targetBridge32Hash) {
    Write-Host "install_bridge32=$targetBridge32"
    Write-Host "install_bridge32_hash=$targetBridge32Hash"
  }
}

function Stop-KeepsakeInstallProcesses {
  $names = @("reaper", "keepsake-bridge")
  foreach ($name in $names) {
    $procs = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
    foreach ($proc in $procs) {
      try {
        Stop-Process -Id $proc.Id -Force -ErrorAction Stop
        Write-Host "stopped_process=$($proc.ProcessName) pid=$($proc.Id)"
      } catch {
        Write-Host "stop_process_failed=$name pid=$($proc.Id) message=$($_.Exception.Message)"
      }
    }
  }

  Start-Sleep -Milliseconds 300
}

if ([string]::IsNullOrWhiteSpace($TargetBundle)) {
  $TargetBundle = Join-Path ${env:CommonProgramFiles} "CLAP\keepsake.clap"
}

$SourceBundle = [System.IO.Path]::GetFullPath($SourceBundle)
$TargetBundle = [System.IO.Path]::GetFullPath($TargetBundle)

if (-not (Test-IsAdministrator)) {
  try {
    Copy-KeepsakeInstall -BundleSource $SourceBundle -BundleTarget $TargetBundle
    exit 0
  } catch [System.UnauthorizedAccessException] {
  } catch [System.Management.Automation.MethodInvocationException] {
    if ($_.Exception.InnerException -isnot [System.UnauthorizedAccessException]) {
      throw
    }
  }

  $argList = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", "`"$PSCommandPath`"",
    "-SourceBundle", "`"$SourceBundle`"",
    "-TargetBundle", "`"$TargetBundle`"",
    "-ElevatedPassThru"
  )

  $proc = Start-Process -FilePath "powershell.exe" -ArgumentList $argList -Verb RunAs -Wait -PassThru
  exit $proc.ExitCode
}

Stop-KeepsakeInstallProcesses
Copy-KeepsakeInstall -BundleSource $SourceBundle -BundleTarget $TargetBundle
