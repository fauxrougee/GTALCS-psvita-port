#!/usr/bin/env bash
# Native Linux/macOS wrapper. On Windows use build-windows.ps1 instead.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$SCRIPT_DIR/build.py" "$@"
