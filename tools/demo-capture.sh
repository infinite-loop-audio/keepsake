#!/usr/bin/env bash

set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <log-path> <command> [args...]" >&2
    exit 1
fi

log_path="$1"
shift

mkdir -p "$(dirname "$log_path")"
"$@" 2>&1 | tee "$log_path"
