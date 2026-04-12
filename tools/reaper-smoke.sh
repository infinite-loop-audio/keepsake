#!/bin/zsh
set -euo pipefail

usage() {
  cat <<'EOF'
usage: tools/reaper-smoke.sh <plugin-id> --vst-path <path> [options]

options:
  --clap-bundle <path>       Path to keepsake.clap bundle (default: build/keepsake.clap)
  --reaper <path>            Path to REAPER binary
  --timeout-sec <n>          Hard timeout for the whole smoke run (default: 30)
  --scan-timeout-ms <n>      Time budget for REAPER to discover the target FX (default: 15000)
  --settle-ms <n>            Extra settle window after add-FX succeeds (default: 1500)
  --open-ui                  After add-FX, open the plugin UI and wait for REAPER to report it open
  --ui-timeout-ms <n>        Time budget for FX UI open in REAPER (default: 5000)
  --run-transport            After setup, create a short MIDI item and run REAPER play/stop
  --play-timeout-ms <n>      Time budget for REAPER play and stop transitions (default: 5000)
  --play-hold-ms <n>         How long to stay in play before stopping (default: 1000)
  --use-default-config       Run against REAPER's normal config instead of an isolated temp config
  --sync-default-install     In default-config mode, sync/touch the installed keepsake.clap before launch
  --keep-temp                Preserve the temp directory for inspection
EOF
}

if [[ $# -lt 3 ]]; then
  usage
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
      usage
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

cleanup() {
  if [[ "${reaper_pid:-}" != "" ]] && kill -0 "$reaper_pid" 2>/dev/null; then
    kill "$reaper_pid" 2>/dev/null || true
    sleep 1
    kill -9 "$reaper_pid" 2>/dev/null || true
  fi

  if [[ "$keep_temp" -eq 0 ]]; then
    rm -rf "$tmp_dir"
  else
    echo "temp dir preserved at $tmp_dir"
  fi
}
trap cleanup EXIT

mkdir -p "$vst_scan_dir"
mkdir -p "$empty_vst_dir"
ln -s "$vst_path" "$vst_scan_dir/$(basename "$vst_path")"

cfg_args=()
if [[ "$use_default_config" -eq 0 ]]; then
cat >"$config_ini" <<EOF
[REAPER]
multinst=1
splash=0
splash2=0
splashfast=1
newprojtmpl=
vstpath_arm64=$empty_vst_dir
clap_path_macos-aarch64=$(cd "$(dirname "$clap_bundle")" && pwd)
EOF

: >"$clap_ini"
cfg_args=(-cfgfile "$config_ini")
fi

if [[ "$sync_default_install" -eq 1 ]]; then
  mkdir -p "$(dirname "$default_install_bundle")"
  mkdir -p "$default_install_bundle"
  rsync -a --delete "$clap_bundle/" "$default_install_bundle/"
  touch "$default_install_bundle" "$default_install_bundle/Contents/MacOS/keepsake"
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
