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
  $targetBridge = Join-Path $targetDir "keepsake-bridge.exe"

  if (-not (Test-Path $BundleSource)) {
    throw "CLAP bundle not found: $BundleSource"
  }
  if (-not (Test-Path $sourceBridge)) {
    throw "bridge binary not found next to CLAP artifact: $sourceBridge"
  }

  $null = New-Item -ItemType Directory -Path $targetDir -Force
  Copy-Item -LiteralPath $BundleSource -Destination $BundleTarget -Force
  Copy-Item -LiteralPath $sourceBridge -Destination $targetBridge -Force

  $stamp = Get-Date
  Get-Item -LiteralPath $BundleTarget | ForEach-Object { $_.LastWriteTime = $stamp }
  Get-Item -LiteralPath $targetBridge | ForEach-Object { $_.LastWriteTime = $stamp }
  Get-Item -LiteralPath $targetDir | ForEach-Object { $_.LastWriteTime = $stamp }

  Write-Host "install_bundle=$BundleTarget"
  Write-Host "install_bridge=$targetBridge"
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

Copy-KeepsakeInstall -BundleSource $SourceBundle -BundleTarget $TargetBundle
