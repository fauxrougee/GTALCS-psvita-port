#!/usr/bin/env bash
# Native Linux SDK setup. Existing directories and shell profiles are retained.
set -euo pipefail
if [[ "$(uname -r)" == *Microsoft ]]; then
    echo "WSL 1 is unsupported here; use the native Windows SDK and build.py." >&2
    exit 1
fi
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
export VITASDK="${VITASDK:-$HOME/vitasdk}"
export PATH="$VITASDK/bin:$PATH"
if [[ ! -x "$VITASDK/bin/arm-vita-eabi-gcc" ]]; then
    if [[ ! -d "$ROOT/sdk/vdpm" ]]; then
        git clone --depth 1 https://github.com/vitasdk/vdpm "$ROOT/sdk/vdpm"
    fi
    bash "$ROOT/sdk/vdpm/bootstrap-vitasdk.sh"
fi
vdpm install zlib taihen kubridge libmathneon vitaShaRK SceShaccCgExt vitaGL openal-soft mpg123
printf 'VitaSDK ready at %s. Set VITASDK to this path when building.\n' "$VITASDK"
