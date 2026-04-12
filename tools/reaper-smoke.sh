#!/bin/zsh
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
source "$script_dir/reaper-smoke-lib.sh"

if [[ $# -lt 3 ]]; then
  reaper_smoke_usage
  exit 1
fi

plugin_id="$1"
shift

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
clap_bundle="$repo_root/build/keepsake.clap"
reaper_bin="/Applications/REAPER.app/Contents/MacOS/REAPER"
timeout_sec=30
scan_timeout_ms=15000
settle_ms=1500
open_ui=0
ui_timeout_ms=5000
run_transport=0
play_timeout_ms=5000
play_hold_ms=1000
keep_temp=0
use_default_config=0
sync_default_install=0
vst_path=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --vst-path)
      vst_path="${2:-}"
      shift 2
      ;;
    --clap-bundle)
      clap_bundle="${2:-}"
      shift 2
      ;;
    --reaper)
      reaper_bin="${2:-}"
      shift 2
      ;;
    --timeout-sec)
      timeout_sec="${2:-}"
      shift 2
      ;;
    --scan-timeout-ms)
      scan_timeout_ms="${2:-}"
      shift 2
      ;;
    --settle-ms)
      settle_ms="${2:-}"
      shift 2
      ;;
    --open-ui)
      open_ui=1
      shift
      ;;
    --ui-timeout-ms)
      ui_timeout_ms="${2:-}"
      shift 2
      ;;
    --run-transport)
      run_transport=1
      shift
      ;;
    --play-timeout-ms)
      play_timeout_ms="${2:-}"
      shift 2
      ;;
    --play-hold-ms)
      play_hold_ms="${2:-}"
      shift 2
      ;;
    --keep-temp)
      keep_temp=1
      shift
      ;;
    --use-default-config)
      use_default_config=1
      shift
      ;;
    --sync-default-install)
      sync_default_install=1
      shift
      ;;
    *)
      echo "unknown argument: $1" >&2
      reaper_smoke_usage
      exit 1
      ;;
  esac
done

if [[ -z "$vst_path" ]]; then
  echo "--vst-path is required" >&2
  exit 1
fi

if [[ ! -x "$reaper_bin" ]]; then
  echo "REAPER binary not found: $reaper_bin" >&2
  exit 1
fi

if [[ ! -d "$clap_bundle" ]]; then
  echo "CLAP bundle not found: $clap_bundle" >&2
  exit 1
fi

if [[ ! -e "$vst_path" ]]; then
  echo "VST path not found: $vst_path" >&2
  exit 1
fi

if [[ "$sync_default_install" -eq 1 && "$use_default_config" -eq 0 ]]; then
  echo "--sync-default-install requires --use-default-config" >&2
  exit 1
fi

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/keepsake-reaper-smoke.XXXXXX")"
config_ini="$tmp_dir/reaper.ini"
clap_ini="$tmp_dir/reaper-clap-macos-aarch64.ini"
status_file="$tmp_dir/status.txt"
log_file="$tmp_dir/reaper-smoke.log"
stdout_file="$tmp_dir/reaper.stdout.log"
vst_scan_dir="$tmp_dir/vst-targets"
empty_vst_dir="$tmp_dir/empty-vst"
script_path="$repo_root/tools/reaper-smoke.lua"
default_install_bundle="$HOME/Library/Audio/Plug-Ins/CLAP/keepsake.clap"
default_reaper_clap_ini="$HOME/Library/Application Support/REAPER/reaper-clap-macos-aarch64.ini"
reaper_clap_backup="$tmp_dir/reaper-clap.backup.ini"

trap reaper_smoke_cleanup EXIT

mkdir -p "$vst_scan_dir"
mkdir -p "$empty_vst_dir"
ln -s "$vst_path" "$vst_scan_dir/$(basename "$vst_path")"

cfg_args=()
if [[ "$use_default_config" -eq 0 ]]; then
  reaper_smoke_write_isolated_config
fi

if [[ "$sync_default_install" -eq 1 ]]; then
  reaper_smoke_sync_default_install
fi

if [[ "$use_default_config" -eq 1 ]]; then
  if [[ -f "$default_reaper_clap_ini" ]]; then
    cp "$default_reaper_clap_ini" "$reaper_clap_backup"
  else
    : >"$reaper_clap_backup"
  fi
fi

echo "plugin_id=$plugin_id"
echo "vst_path=$vst_path"
echo "clap_bundle=$clap_bundle"
echo "temp_dir=$tmp_dir"
echo "log_file=$log_file"
echo "config_mode=$([[ \"$use_default_config\" -eq 1 ]] && echo default || echo isolated)"
echo "open_ui=$open_ui"
echo "run_transport=$run_transport"
if [[ "$sync_default_install" -eq 1 ]]; then
  echo "default_install=$default_install_bundle"
fi

env \
  CLAP_PATH="" \
  KEEPSAKE_VST2_PATH="$vst_scan_dir" \
  KEEPSAKE_REAPER_SMOKE_PLUGIN_ID="$plugin_id" \
  KEEPSAKE_REAPER_SMOKE_LOG="$log_file" \
  KEEPSAKE_REAPER_SMOKE_STATUS="$status_file" \
  KEEPSAKE_REAPER_SMOKE_SCAN_TIMEOUT_MS="$scan_timeout_ms" \
  KEEPSAKE_REAPER_SMOKE_SETTLE_MS="$settle_ms" \
  KEEPSAKE_REAPER_SMOKE_OPEN_UI="$open_ui" \
  KEEPSAKE_REAPER_SMOKE_UI_TIMEOUT_MS="$ui_timeout_ms" \
  KEEPSAKE_REAPER_SMOKE_RUN_TRANSPORT="$run_transport" \
  KEEPSAKE_REAPER_SMOKE_PLAY_TIMEOUT_MS="$play_timeout_ms" \
  KEEPSAKE_REAPER_SMOKE_PLAY_HOLD_MS="$play_hold_ms" \
  "$reaper_bin" -newinst -nosplash -new "${cfg_args[@]}" "$script_path" \
  >"$stdout_file" 2>&1 &
reaper_pid=$!

deadline=$((SECONDS + timeout_sec))
result=""

while kill -0 "$reaper_pid" 2>/dev/null; do
  if [[ -f "$status_file" ]]; then
    result="$(tr -d '\r\n' < "$status_file")"
    break
  fi
  if (( SECONDS >= deadline )); then
    result="TIMEOUT"
    break
  fi
  sleep 1
done

if [[ -z "$result" && -f "$status_file" ]]; then
  result="$(tr -d '\r\n' < "$status_file")"
fi

if kill -0 "$reaper_pid" 2>/dev/null; then
  kill "$reaper_pid" 2>/dev/null || true
  wait "$reaper_pid" 2>/dev/null || true
else
  wait "$reaper_pid" 2>/dev/null || true
fi

echo
if [[ -f "$log_file" ]]; then
  sed -n '1,200p' "$log_file"
else
  echo "no smoke log was produced"
fi

echo
echo "reaper_stdout=$stdout_file"

case "$result" in
  PASS)
    echo "result=PASS"
    ;;
  FAIL)
    echo "result=FAIL"
    exit 1
    ;;
  TIMEOUT)
    echo "result=TIMEOUT"
    exit 124
    ;;
  *)
    echo "result=UNKNOWN"
    exit 1
    ;;
esac
