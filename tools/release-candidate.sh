#!/bin/zsh
set -euo pipefail

usage() {
  cat <<'EOF'
usage: tools/release-candidate.sh --version <label> [options]

options:
  --version <label>     Release label for artifact names, e.g. v0.1-alpha
  --build-dir <path>    Build directory (default: build)
  --output-dir <path>   Output directory (default: dist/<label>)
EOF
}

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="$repo_root/build"
version_label=""
output_dir=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      version_label="${2:-}"
      shift 2
      ;;
    --build-dir)
      build_dir="${2:-}"
      shift 2
      ;;
    --output-dir)
      output_dir="${2:-}"
      shift 2
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -z "$version_label" ]]; then
  usage
  exit 1
fi

if [[ -z "$output_dir" ]]; then
  output_dir="$repo_root/dist/$version_label"
fi

if [[ ! -d "$build_dir/keepsake.clap" && ! -f "$build_dir/keepsake.clap" ]]; then
  echo "missing built artifact: $build_dir/keepsake.clap" >&2
  exit 1
fi

platform=""
archive_name=""
clap_path="$build_dir/keepsake.clap"

case "$(uname -s)" in
  Darwin)
    platform="macos-arm64"
    archive_name="keepsake-${platform}-${version_label}.zip"
    ;;
  Linux)
    platform="linux-x64"
    archive_name="keepsake-${platform}-${version_label}.tar.gz"
    ;;
  MINGW*|MSYS*|CYGWIN*|Windows_NT)
    platform="windows-x64"
    archive_name="keepsake-${platform}-${version_label}.zip"
    ;;
  *)
    echo "unsupported platform: $(uname -s)" >&2
    exit 1
    ;;
esac

artifact_path="$output_dir/$archive_name"
checksum_path="$output_dir/SHA256SUMS.txt"

rm -rf "$output_dir"
mkdir -p "$output_dir"
stage_dir="$output_dir/stage"
mkdir -p "$stage_dir"

copy_helper_if_present() {
  local helper_name="$1"
  if [[ -f "$build_dir/$helper_name" ]]; then
    cp "$build_dir/$helper_name" "$stage_dir/"
  fi
}

if [[ -d "$clap_path" ]]; then
  cp -R "$clap_path" "$stage_dir/"
else
  cp "$clap_path" "$stage_dir/"
fi

if [[ "$platform" != macos-* ]]; then
  copy_helper_if_present "keepsake-bridge"
  copy_helper_if_present "keepsake-bridge-x86_64"
fi

case "$archive_name" in
  *.zip)
    (
      cd "$stage_dir"
      ditto -c -k --sequesterRsrc --keepParent "keepsake.clap" "$artifact_path"
    )
    ;;
  *.tar.gz)
    tar -C "$stage_dir" -czf "$artifact_path" .
    ;;
esac

if command -v shasum >/dev/null 2>&1; then
  (
    cd "$output_dir"
    shasum -a 256 "$(basename "$artifact_path")" >"$checksum_path"
  )
elif command -v sha256sum >/dev/null 2>&1; then
  (
    cd "$output_dir"
    sha256sum "$(basename "$artifact_path")" >"$checksum_path"
  )
else
  echo "no sha256 tool found" >&2
  exit 1
fi

echo "release_label=$version_label"
echo "platform=$platform"
echo "artifact=$artifact_path"
echo "checksums=$checksum_path"
