#!/usr/bin/env bash
# Read-only host dependency summary for native Linux builds.
for tool in git cmake ninja python3 ffmpeg ffprobe; do
    command -v "$tool" || printf 'Missing: %s\n' "$tool"
done
printf 'VITASDK=%s\n' "${VITASDK:-not set}"
