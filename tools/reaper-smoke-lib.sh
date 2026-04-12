reaper_smoke_usage() {
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

reaper_smoke_restore_default_clap_cache() {
  python3 - "$reaper_clap_backup" "$default_reaper_clap_ini" <<'PY'
from pathlib import Path
import sys

backup_path = Path(sys.argv[1])
target_path = Path(sys.argv[2])
section_name = "keepsake.clap"

def parse_sections(text):
    lines = text.splitlines(keepends=True)
    sections = []
    current_name = None
    current_lines = []
    for line in lines:
        if line.startswith("[") and "]" in line:
            if current_name is not None:
                sections.append((current_name, current_lines))
            current_name = line.strip()[1:-1]
            current_lines = [line]
        elif current_name is None:
            sections.append((None, [line]))
        else:
            current_lines.append(line)
    if current_name is not None:
        sections.append((current_name, current_lines))
    return sections

backup_text = backup_path.read_text() if backup_path.exists() else ""
target_text = target_path.read_text() if target_path.exists() else ""

backup_sections = parse_sections(backup_text)
target_sections = parse_sections(target_text)

backup_keepsake = None
for name, lines in backup_sections:
    if name == section_name:
        backup_keepsake = lines
        break

result = []
inserted = False
for name, lines in target_sections:
    if name == section_name:
        if backup_keepsake is not None:
            result.extend(backup_keepsake)
            inserted = True
        continue
    result.extend(lines)

if backup_keepsake is not None and not inserted:
    if result and not result[-1].endswith("\n"):
        result[-1] += "\n"
    if result and result[-1] != "\n":
        result.append("\n")
    result.extend(backup_keepsake)

target_path.parent.mkdir(parents=True, exist_ok=True)
target_path.write_text("".join(result))
PY
}

reaper_smoke_write_isolated_config() {
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
}

reaper_smoke_sync_default_install() {
  mkdir -p "$(dirname "$default_install_bundle")"
  mkdir -p "$default_install_bundle"
  rsync -a --delete "$clap_bundle/" "$default_install_bundle/"
  touch "$default_install_bundle" "$default_install_bundle/Contents/MacOS/keepsake"
}

reaper_smoke_cleanup() {
  if [[ "${reaper_pid:-}" != "" ]] && kill -0 "$reaper_pid" 2>/dev/null; then
    kill "$reaper_pid" 2>/dev/null || true
    sleep 1
    kill -9 "$reaper_pid" 2>/dev/null || true
  fi

  if [[ "$use_default_config" -eq 1 && -f "$reaper_clap_backup" ]]; then
    reaper_smoke_restore_default_clap_cache
  fi

  if [[ "$keep_temp" -eq 0 ]]; then
    rm -rf "$tmp_dir"
  else
    echo "temp dir preserved at $tmp_dir"
  fi
}
