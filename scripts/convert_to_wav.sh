#!/usr/bin/env bash
# Convert an input audio file (m4a, mp3, etc.) to 16 kHz mono 16-bit PCM WAV.
#
# Usage: scripts/convert_to_wav.sh <input> [output.wav]
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: $0 <input-audio> [output.wav]" >&2
    exit 1
fi

input=$1
output=${2:-audio.wav}

ffmpeg -hide_banner -y -i "$input" -c:a pcm_s16le -ar 16000 -ac 1 "$output"

echo "Wrote $output"
